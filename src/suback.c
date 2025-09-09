#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"

#define DEFAULT_SUBACK_BYTE 0x90

void mqtt_suback_write(Tera_Context *ctx, const Client_Data *cdata, const Subscribe_Result *r)
{
    if (r->acknowledged || r->topic_filter_count == 0)
        return;

    Buffer *buf         = &ctx->connection_data[cdata->conn_id].send_buffer;
    isize bytes_written = 0;

    buffer_reset(buf);

    // Fixed Header
    bytes_written += buffer_write_struct(buf, "B", DEFAULT_SUBACK_BYTE);

    // Calculate remaining length: packet_id(2) + properties_length(1) + reason_codes(n)
    usize remaining_length = sizeof(uint16) + sizeof(uint8) + r->topic_filter_count;
    bytes_written += mqtt_variable_length_write(buf, remaining_length);

    // Variable Header (0 properties length)
    bytes_written += buffer_write_struct(buf, "HB", r->packet_id, 0);

    for (usize i = 0; i < r->topic_filter_count; ++i) {
        bytes_written += buffer_write_struct(buf, "B", r->reason_codes[i]);
    }

    log_info("sent: SUBACK %zd bytes, packet_id: %d, topics: %d", bytes_written, r->packet_id,
             r->topic_filter_count);
}
