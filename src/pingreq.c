#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"

MQTT_Decode_Result mqtt_pingreq_read(Tera_Context *ctx, const Client_Data *cdata)
{
    Buffer *buf               = &ctx->connection_data[cdata->conn_id].recv_buffer;
    Fixed_Header header       = {0};

    MQTT_Decode_Result result = mqtt_fixed_header_read(buf, &header);

    if (result == MQTT_DECODE_SUCCESS)
        log_info("recv: PINGREQ");

    return result;
}
