#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"

#define PROTOCOL_NAME_BYTES_LEN 6

MQTT_Decode_Result mqtt_connect_read(Tera_Context *ctx, Client_Data *cdata)
{
    if (ctx->connection_data[cdata->conn_id].connected) {
        /*
         * Already connected client, 2 CONNECT packet should be interpreted as
         * a violation of the protocol, causing disconnection of the client
         */
        log_info(">>>>: received double CONNECT, disconnecting client");
        return MQTT_DECODE_INVALID;
    }

    Buffer *buf     = &ctx->connection_data[cdata->conn_id].recv_buffer;
    usize start_pos = buf->read_pos;

    // Read packet type and flags
    if (buffer_read_struct(buf, "B", &(uint8){0}) != sizeof(uint8)) {
        log_error("Failed to read packet header");
        return MQTT_DECODE_ERROR;
    }

    // Read remaining length
    usize packet_length = 0;
    int length_bytes    = mqtt_variable_length_read(buf, &packet_length);
    if (length_bytes < 0) {
        log_error("Invalid variable length encoding");
        buf->read_pos = start_pos; // Restore position
        return MQTT_DECODE_INCOMPLETE;
    }

    // Validate we have enough data for the complete packet
    usize total_packet_size =
        sizeof(uint8) + length_bytes + packet_length; // header + length + payload
    if (start_pos + total_packet_size > buf->size) {
        log_debug("Incomplete packet - need %zu more bytes",
                  (start_pos + total_packet_size) - buf->size);
        buf->read_pos = start_pos; // Restore position
        return MQTT_DECODE_INCOMPLETE;
    }

    usize memory_offset = arena_current_offset(ctx->client_arena);
    uint8 *ptr          = arena_alloc(ctx->client_arena, packet_length);
    if (!ptr) {
        // TODO handle case
        log_critical("bump arena OOM");
    }

    cdata->client_id_size = memory_offset;

    // === VARIABLE HEADER ===

    // 1. Protocol Name - Ignore the 'M' 'Q' 'T' 'T' bytes
    if (buffer_skip(buf, PROTOCOL_NAME_BYTES_LEN) != PROTOCOL_NAME_BYTES_LEN)
        return MQTT_DECODE_ERROR;

    // 2. Protocol Version
    uint8 protocol_version;
    if (buffer_read_struct(buf, "B", &protocol_version) != sizeof(uint8))
        return MQTT_DECODE_ERROR;

    if (protocol_version != 0x05) {
        log_error("Unsupported MQTT version: %d", protocol_version);
        // TODO Should send CONNACK with 0x84 (Unsupported Protocol Version)
        return MQTT_DECODE_ERROR;
    }

    // Read variable header byte flags, followed by keepalive MSB and LSB
    // (2 bytes word) and the client ID length (2 bytes here again)
    if (buffer_read_struct(buf, "BH", &cdata->connect_flags, &cdata->keepalive) !=
        sizeof(uint8) + sizeof(uint16))
        return MQTT_DECODE_ERROR;

    // 4. Properties Length + Properties
    usize properties_length = 0;
    int prop_length_bytes   = mqtt_variable_length_read(buf, &properties_length);
    if (prop_length_bytes < 0)
        return MQTT_DECODE_ERROR;

    // Skip Properties for now (should be parsed in full implementation)
    if (buffer_skip(buf, properties_length) != properties_length)
        return MQTT_DECODE_ERROR;

    log_info("recv: CONNECT (p%d c%d k%d)", protocol_version, cdata->connect_flags,
             cdata->keepalive);

    // === PAYLOAD ===

    // 1. Client Identifier
    uint16 client_id_size = 0;
    if (buffer_read_struct(buf, "H", &client_id_size) != sizeof(uint16))
        return MQTT_DECODE_ERROR;
    cdata->client_id_size   = client_id_size;
    cdata->client_id_offset = memory_offset;

    // Read the client ID
    if (cdata->client_id_size > 0) {
        if (buffer_read_binary(ptr, buf, cdata->client_id_size) != cdata->client_id_size)
            return MQTT_DECODE_ERROR;
        ptr += cdata->client_id_size;
        memory_offset += cdata->client_id_size;
    }

    // 2. Read the will topic and message if will is set
    if (mqtt_will_get(cdata->connect_flags)) {
        usize will_properties_length = 0;
        int will_prop_bytes          = mqtt_variable_length_read(buf, &will_properties_length);
        if (will_prop_bytes < 0) {
            return MQTT_DECODE_ERROR;
        }

        // Skip Will Properties
        if (buffer_skip(buf, will_properties_length) != will_properties_length) {
            return MQTT_DECODE_ERROR;
        }

        // Topic
        if (buffer_read_struct(buf, "H", &cdata->will_topic_size) != sizeof(uint16))
            return MQTT_DECODE_ERROR;

        cdata->will_topic_offset = memory_offset;
        if (buffer_read_binary(ptr, buf, cdata->will_topic_size) != cdata->will_topic_size)
            return MQTT_DECODE_ERROR;

        ptr += cdata->will_topic_size;
        memory_offset += cdata->will_topic_size;

        // Message
        if (buffer_read_struct(buf, "H", &cdata->will_message_size) != sizeof(uint16))
            return MQTT_DECODE_ERROR;

        cdata->will_message_offset = memory_offset;
        if (buffer_read_binary(ptr, buf, cdata->will_message_size) != cdata->will_message_size)
            return MQTT_DECODE_ERROR;

        ptr += cdata->will_message_size;
        memory_offset += cdata->will_message_size;
    }

    // Read the username if username flag is set
    if (mqtt_username_get(cdata->connect_flags)) {
        if (buffer_read_struct(buf, "H", &cdata->username_size) != sizeof(uint16))
            return MQTT_DECODE_ERROR;

        cdata->username_offset = memory_offset;
        if (buffer_read_binary(ptr, buf, cdata->username_size) != cdata->username_size)
            return MQTT_DECODE_ERROR;

        ptr += cdata->username_size;
        memory_offset += cdata->username_size;
    }

    // Read the password if password flag is set
    if (mqtt_password_get(cdata->connect_flags)) {
        if (buffer_read_struct(buf, "H", &cdata->password_size) != sizeof(uint16))
            return MQTT_DECODE_ERROR;

        cdata->password_offset = memory_offset;
        if (buffer_read_binary(ptr, buf, cdata->password_size) != cdata->password_size)
            return MQTT_DECODE_ERROR;

        ptr += cdata->password_size;
        memory_offset += cdata->password_size;
    }

    ctx->connection_data[cdata->conn_id].connected = true;

    return MQTT_DECODE_SUCCESS;
}
