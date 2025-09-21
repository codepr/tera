#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"

MQTT_Decode_Result mqtt_disconnect_read(Tera_Context *ctx, const Client_Data *cdata)
{
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

    uint8 reason_code;
    if (buffer_read_struct(buf, "B", &reason_code) != sizeof(uint8))
        return MQTT_DECODE_ERROR;

    if (cdata->mqtt_version == MQTT_V5) {
        usize properties_length = 0;
        int prop_length_bytes   = mqtt_variable_length_read(buf, &properties_length);
        if (prop_length_bytes < 0)
            return MQTT_DECODE_ERROR;

        // TODO Skip expiry
        if (buffer_skip(buf, 5) != 5)
            log_debug("Expiry incomplete - need %zu more bytes",
                      (start_pos + total_packet_size) - buf->size);
    }

    log_info("recv: DISCONNECT rc: %d", reason_code);
    return MQTT_DECODE_SUCCESS;
}
