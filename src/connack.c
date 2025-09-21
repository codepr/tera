#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"

#define DEFAULT_CONNACK_BYTE 0x20

/*
 * According to MQTT v5.0 spec, CONNACK packet format is:
 *
 * Fixed Header:
 * - Byte 1: 0x20 (CONNACK packet type with reserved flags)
 * - Byte 2: Remaining Length
 *
 * Variable Header:
 * - Byte 1: Connect Acknoledgement Flags
 * - Byte 2: Connack Reason Code
 * - Byte 3: Properties Length
 * - Properties if present
 *
 * Payload: None
 *
 */
void mqtt_connack_write(Tera_Context *ctx, const Client_Data *cdata, CONNACK_Reason_Code rc)
{
    Buffer *buf             = &ctx->connection_data[cdata->conn_id].send_buffer;
    uint8 session_present   = 0;
    uint8 properties_length = 0;

    // TODO clean session logic

    uint8 connect_ack_flags = session_present & 0x01;
    buffer_reset(buf);

    // Fixed Header
    isize bytes_written    = buffer_write_struct(buf, "B", DEFAULT_CONNACK_BYTE);

    // Remaining length = flags + rc
    uint8 remaining_length = sizeof(uint8) * 2;
    if (cdata->mqtt_version == MQTT_V5)
        // + properties if MQTT v5
        remaining_length += sizeof(uint8) + properties_length;

    bytes_written += mqtt_variable_length_write(buf, remaining_length);
    bytes_written +=
        cdata->mqtt_version == MQTT_V5
            ? buffer_write_struct(buf, "BBB", connect_ack_flags, (uint8)rc, properties_length)
            : buffer_write_struct(buf, "BB", connect_ack_flags, (uint8)rc);

    // TODO properties if present

    log_info("sent: CONNACK %zd bytes, sp: %d rc: 0x%02X", bytes_written, session_present, rc);
}
