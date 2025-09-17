#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"

MQTT_Decode_Result mqtt_ack_read(Tera_Context *ctx, const Client_Data *cdata, int16 *mid)
{
    Buffer *buf         = &ctx->connection_data[cdata->conn_id].recv_buffer;
    Fixed_Header header = {0};
    uint8 rc            = 0;

    isize result        = mqtt_fixed_header_read(buf, &header);
    if (result < 0)
        return MQTT_DECODE_ERROR;

    // TODO properties

    result = MQTT_DECODE_SUCCESS;
    switch (header.bits.type) {
    case PUBACK:
        buffer_read_struct(buf, "HB", mid, &rc);
        log_info("recv: PUBACK mid: %d rc: 0x00", *mid);
        break;
    case PUBREC:
        buffer_read_struct(buf, "HB", mid, &rc);
        log_info("recv: PUBREC mid: %d rc: 0x00", *mid);
        break;
    case PUBREL:
        buffer_read_struct(buf, "HB", mid, &rc);
        log_info("recv: PUBREL mid: %d rc: 0x00", *mid);
        break;
    case PUBCOMP:
        buffer_read_struct(buf, "HB", mid, &rc);
        log_info("recv: PUBCOMP mid: %d rc: 0x00", *mid);
        break;
    }

    return result;
}

#define DEFAULT_PUBACK_BYTE  0x40
#define DEFAULT_PUBREC_BYTE  0x50
#define DEFAULT_PUBREL_BYTE  0x62
#define DEFAULT_PUBCOMP_BYTE 0x70

void mqtt_ack_write(Tera_Context *ctx, const Client_Data *cdata, Packet_Type ack_type, int16 id)
{
    Buffer *buf         = &ctx->connection_data[cdata->conn_id].send_buffer;
    // remaining length of 2 means success by default
    Fixed_Header header = {.remaining_length = 2};

    // TODO handle reason codes, 0x00 is success
    switch (ack_type) {
    case PUBACK:
        header.byte = DEFAULT_PUBACK_BYTE;
        mqtt_fixed_header_write(buf, &header);
        buffer_write_struct(buf, "H", id);
        log_info("sent: PUBACK mid: %d rc: 0x00", id);
        break;
    case PUBREC:
        header.byte = DEFAULT_PUBREC_BYTE;
        mqtt_fixed_header_write(buf, &header);
        buffer_write_struct(buf, "H", id);
        log_info("sent: PUBREC mid: %d rc: 0x00", id);
        break;
    case PUBREL:
        header.byte = DEFAULT_PUBREL_BYTE;
        mqtt_fixed_header_write(buf, &header);
        buffer_write_struct(buf, "H", id);
        log_info("sent: PUBREL mid: %d rc: 0x00", id);
        break;
    case PUBCOMP:
        header.byte = DEFAULT_PUBCOMP_BYTE;
        mqtt_fixed_header_write(buf, &header);
        buffer_write_struct(buf, "H", id);
        log_info("sent: PUBCOMP mid: %d rc: 0x00", id);
        break;
    default:
        // Unreachable
        break;
    }
}
