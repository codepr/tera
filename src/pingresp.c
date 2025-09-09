#include "mqtt.h"
#include "tera_internal.h"

#define DEFAULT_PINGRESP_BYTE 0xD0

void mqtt_pingresp_write(Tera_Context *ctx, const Client_Data *cdata)
{
    Buffer *buf         = &ctx->connection_data[cdata->conn_id].send_buffer;
    Fixed_Header header = {.byte = DEFAULT_PINGRESP_BYTE, .remaining_length = 0};

    isize written_bytes = mqtt_fixed_header_write(buf, &header);

    log_info("send: PINGRESP %ld bytes", written_bytes);
}
