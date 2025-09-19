#include "arena.h"
#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"

static Subscription_Data *find_free_subscription_slot(Tera_Context *ctx)
{
    for (usize i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
        if (ctx->subscription_data[i].active)
            continue;

        return &ctx->subscription_data[i];
    }

    return NULL;
}

/**
 * Validates that a subscription topic filter follows MQTT wildcard rules
 * Should be called when processing SUBSCRIBE packets
 */
static bool topic_filter_is_valid(const char *filter, usize filter_size)
{
    if (filter_size == 0)
        return false;

    for (usize i = 0; i < filter_size; ++i) {
        if (filter[i] == '#') {
            // '#' must be at the end
            if (i != filter_size - 1)
                return false;

            // '#' must be alone or preceded by '/'
            if (i > 0 && filter[i - 1] != '/')
                return false;
        }

        // Check for valid '+' filter
        if (filter[i] == '+') {
            // '+' must be alone in its level
            bool valid_plus = true;

            if (i > 0 && filter[i - 1] != '/')
                valid_plus = false;

            if (i < filter_size - 1 && filter[i + 1] != '/')
                valid_plus = false;

            if (!valid_plus)
                return false;
        }
    }

    return true;
}

MQTT_Decode_Result mqtt_subscribe_read(Tera_Context *ctx, const Client_Data *cdata,
                                       Subscribe_Result *r)
{
    uint16 id       = 0;
    Buffer *buf     = &ctx->connection_data[cdata->conn_id].recv_buffer;
    usize start_pos = buf->read_pos;

    r->acknowledged = false;

    // Read packet type and flags
    if (buffer_read_struct(buf, "B", &(uint8){0}) != 1) {
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

    if (buffer_read_struct(buf, "H", &id) != sizeof(uint16))
        return MQTT_DECODE_ERROR;
    packet_length -= sizeof(uint16);

    usize properties_length = 0;
    int prop_length_bytes   = mqtt_variable_length_read(buf, &properties_length);
    if (prop_length_bytes < 0)
        return MQTT_DECODE_ERROR;

    packet_length -= prop_length_bytes;

    usize sub_id = 0;
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

    /*
     * Read in a loop all remaining bytes specified by len of the Fixed Header.
     * From now on the payload consists of 3-tuples formed by:
     *  - topic length
     *  - topic filter (string)
     *  - qos
     */
    while (packet_length > 0) {
        Subscription_Data *tdata = find_free_subscription_slot(ctx);
        if (!tdata)
            return MQTT_DECODE_ERROR;
        tdata->client_id = cdata->conn_id;
        tdata->active    = true;
        tdata->id        = sub_id > 0 ? sub_id : -1;
        // Read length bytes of the first topic filter

        if (buffer_read_struct(buf, "H", &tdata->topic_size) != sizeof(uint16))
            return MQTT_DECODE_ERROR;

        packet_length -= sizeof(uint16);

        tdata->topic_offset = arena_current_offset(ctx->topic_arena);
        uint8 *topic_filter = arena_alloc(ctx->topic_arena, tdata->topic_size);
        if (!topic_filter) {
            // TODO handle case
            log_critical("bump arena OOM");
        }

        if (buffer_read_binary(topic_filter, buf, tdata->topic_size) != tdata->topic_size)
            return MQTT_DECODE_ERROR;

        if (!topic_filter_is_valid((const char *)topic_filter, tdata->topic_size))
            return MQTT_DECODE_INVALID;

        // Classify the filter type
        Topic_Filter_Type type = TFT_WILDCARD_NONE;
        uint16 prefix_levels   = 0;

        for (usize i = 0; i < tdata->topic_size; ++i) {
            if (topic_filter[i] == '#') {
                type = TFT_WILDCARD_HASH;
                break;
            } else if (topic_filter[i] == '+') {
                type = TFT_WILDCARD_PLUS;
            } else if (topic_filter[i] == '/') {
                prefix_levels++;
            }
        }

        tdata->type          = type;
        tdata->prefix_levels = prefix_levels;

        packet_length -= tdata->topic_size;

        if (buffer_read_struct(buf, "B", &tdata->options) != sizeof(uint8))
            return MQTT_DECODE_ERROR;

        packet_length -= sizeof(uint8);
        uint8 qos = tdata->options & 0x03;

        // TODO not the right error
        if (qos < AT_MOST_ONCE || qos > EXACTLY_ONCE)
            r->reason_codes[r->topic_filter_count] = SUBACK_UNSPECIFIED_ERROR;
        else
            // TODO subscription logic (e.g. check for auth, QoS level etc)
            r->reason_codes[r->topic_filter_count] = (SUBACK_Reason_Code)qos;

        log_info("recv: SUBSCRIBE id: %d, sid: %d, cid: %d QoS: %d, rc: 0x%02X", id, tdata->id,
                 tdata->client_id, qos, r->reason_codes[r->topic_filter_count]);

        r->packet_id = id;
        r->topic_filter_count++;
    }

    return MQTT_DECODE_SUCCESS;
}

uint16 mqtt_subscription_next_mid(Subscription_Data *subscription_data)
{
    // TODO check for boundary
    return subscription_data->mid++;
}
