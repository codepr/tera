#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"

MQTT_Decode_Result mqtt_unsubscribe_read(Tera_Context *ctx, const Client_Data *cdata,
                                         Subscribe_Result *r)
{
    (void)r;

    Buffer *buf     = &ctx->connection_data[cdata->conn_id].recv_buffer;
    usize start_pos = buf->read_pos;

    // Read packet type and flags
    if (buffer_read_struct(buf, "B", &(uint8){0}) != 1) {
        log_error("recv: UNSUBSCRIBE - failed to read packet header");
        return MQTT_DECODE_ERROR;
    }

    // Read remaining length
    usize packet_length = 0;
    int length_bytes    = mqtt_variable_length_read(buf, &packet_length);
    if (length_bytes < 0) {
        log_error("recv: UNSUBSCRIBE - invalid variable length encoding");
        buf->read_pos = start_pos; // Restore position
        return MQTT_DECODE_INCOMPLETE;
    }

    // Validate we have enough data for the complete packet
    usize total_packet_size =
        sizeof(uint8) + length_bytes + packet_length; // header + length + payload
    if (start_pos + total_packet_size > buf->size) {
        log_debug("recv: UNSUBSCRIBE - incomplete packet - need %zu more bytes",
                  (start_pos + total_packet_size) - buf->size);
        buf->read_pos = start_pos; // Restore position
        return MQTT_DECODE_INCOMPLETE;
    }

    uint16 id = 0;
    // TODO set ID
    if (buffer_read_struct(buf, "H", &id) != sizeof(uint16))
        return MQTT_DECODE_ERROR;
    packet_length -= sizeof(uint16);

    usize sub_id = 0;

    if (cdata->mqtt_version == MQTT_V5) {
        usize properties_length = 0;
        int prop_length_bytes   = mqtt_variable_length_read(buf, &properties_length);
        if (prop_length_bytes < 0)
            return MQTT_DECODE_ERROR;

        packet_length -= prop_length_bytes;

        if (properties_length > 0) {

            // Skip Properties for now (should be parsed in full implementation)
            if (buffer_skip(buf, sizeof(uint8)) != sizeof(uint8))
                return MQTT_DECODE_ERROR;

            usize sub_id_length = mqtt_variable_length_read(buf, &sub_id);

            if (buffer_skip(buf, properties_length - sizeof(uint8) - sub_id_length) !=
                properties_length - sizeof(uint8) - sub_id_length)
                return MQTT_DECODE_ERROR;

            packet_length -= properties_length;
        }
    }

    /*
     * Read in a loop all remaining bytes specified by len of the Fixed Header.
     * From now on the payload consists of 3-tuples formed by:
     *  - topic length
     *  - topic filter (string)
     *  - qos
     */
    while (packet_length > 0) {
        // TODO
        // Decode topic filters (u16 length + string)
        // Scan existing subscriptions for the current client
        // Collect the indexes in memory of each, take care of wildcards (# and +)
    }

    return MQTT_DECODE_SUCCESS;
}
