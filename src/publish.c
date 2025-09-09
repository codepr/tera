#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"
#include <string.h>

static Message_Data *find_free_message_slot(Tera_Context *ctx)
{
    for (usize i = 0; i < MAX_PACKETS; ++i) {
        if (ctx->message_data[i].active)
            continue;

        ctx->message_data[i].active = true;
        return &ctx->message_data[i];
    }

    return NULL;
}

static Publish_Properties *find_free_property_slot(Tera_Context *ctx, usize *property_id)
{
    // TODO make this useful (id recycling etc etc)
    for (usize i = 0; i < MAX_PACKETS; ++i) {
        if (ctx->properties_data[i].active)
            continue;

        ctx->properties_data[i].active = true;
        *property_id                   = i;
        return &ctx->properties_data[i];
    }

    return NULL;
}

static uint32 calculate_publish_properties_length(const Publish_Properties *props)
{
    uint32 length = 0;

    if (props->has_payload_format) {
        length += sizeof(uint8) * 2; // Property ID + 1 byte value
    }

    if (props->has_message_expiry) {
        length += sizeof(uint8) + sizeof(uint32); // Property ID + 4 byte value
    }

    if (props->has_content_type) {
        length += sizeof(uint8) + sizeof(uint32) +
                  props->content_type_len; // Property ID + length + string
    }

    if (props->has_response_topic) {
        length += sizeof(uint8) + sizeof(uint32) +
                  props->response_topic_len; // Property ID + length + string
    }

    if (props->has_correlation_data) {
        length += sizeof(uint8) + sizeof(uint32) +
                  props->correlation_data_len; // Property ID + length + data
    }

    if (props->has_topic_alias) {
        length += sizeof(uint8) + sizeof(uint32); // Property ID + 2 byte value
    }

    // Subscription Identifiers (multiple allowed!)
    for (int i = 0; i < props->subscription_id_count; i++) {
        length += sizeof(uint8); // Property ID
        length += mqtt_variable_length_encoded_length(props->subscription_ids[i]);
    }

    return length;
}

static MQTT_Decode_Result mqtt_publish_properties_read(Buffer *buf, Publish_Properties *props,
                                                       usize length)
{
    usize bytes_consumed = 0;

    // Initialize properties
    memset(props, 0, sizeof(Publish_Properties));

    while (bytes_consumed < length) {
        uint8 property_id;
        if (buffer_read_struct(buf, "B", &property_id) != sizeof(uint8)) {
            return MQTT_DECODE_ERROR;
        }
        bytes_consumed += sizeof(uint8);

        switch (property_id) {
        case PUBLISH_PROP_PAYLOAD_FORMAT_INDICATOR:
            if (buffer_read_struct(buf, "B", &props->payload_format_indicator) != sizeof(uint8)) {
                return MQTT_DECODE_ERROR;
            }
            props->has_payload_format = true;
            bytes_consumed += sizeof(uint8);
            break;

        case PUBLISH_PROP_MESSAGE_EXPIRY_INTERVAL:
            if (buffer_read_struct(buf, "I", &props->message_expiry_interval) != sizeof(uint32)) {
                return MQTT_DECODE_ERROR;
            }
            props->has_message_expiry = true;
            bytes_consumed += sizeof(uint32);
            break;

        case PUBLISH_PROP_SUBSCRIPTION_IDENTIFIER: {
            if (props->subscription_id_count >= MAX_SUBSCRIPTION_IDS) {
                log_error("Too many subscription identifiers");
                return MQTT_DECODE_ERROR;
            }

            usize sub_id;
            int sub_id_bytes = mqtt_variable_length_read(buf, &sub_id);
            if (sub_id_bytes < 0) {
                return MQTT_DECODE_ERROR;
            }

            props->subscription_ids[props->subscription_id_count] = sub_id;
            props->subscription_id_count++;
            bytes_consumed += sub_id_bytes;

            log_info("Found Subscription ID: %lu", sub_id);
            break;
        }

        case PUBLISH_PROP_TOPIC_ALIAS:
            if (buffer_read_struct(buf, "H", &props->topic_alias) != sizeof(uint16)) {
                return MQTT_DECODE_ERROR;
            }
            props->has_topic_alias = true;
            bytes_consumed += sizeof(uint16);
            break;

            // TODO handle other properties

        default:
            log_warning("Unknown PUBLISH property: 0x%02X", property_id);
            // Skip unknown property - need to determine size based on type
            return MQTT_DECODE_ERROR; // For safety
        }
    }

    return MQTT_DECODE_SUCCESS;
}

MQTT_Decode_Result mqtt_publish_read(Tera_Context *ctx, const Client_Data *cdata)
{
    Buffer *buf            = &ctx->connection_data[cdata->conn_id].recv_buffer;
    usize consumed         = 0;
    Fixed_Header header    = {0};

    isize fixed_header_len = mqtt_fixed_header_read(buf, &header);
    if (fixed_header_len < 0) {
        log_error("Failed to read packet header");
        return MQTT_DECODE_ERROR;
    }

    Message_Data *message = find_free_message_slot(ctx);
    message->qos          = header.bits.qos;
    message->retain       = header.bits.retain;
    message->dup          = header.bits.dup;

    usize memory_offset   = ctx->message_arena->curr_offset;
    uint8 *ptr            = arena_alloc(ctx->message_arena, header.remaining_length);
    if (!ptr) {
        // TODO handle case
        log_critical("bump arena OOM");
    }

    message->topic_offset = memory_offset;
    if (buffer_read_struct(buf, "H", &message->topic_size) != sizeof(uint16))
        return MQTT_DECODE_ERROR;
    consumed += sizeof(uint16);

    if (buffer_read_binary(ptr, buf, message->topic_size) != message->topic_size)
        return MQTT_DECODE_ERROR;
    consumed += message->topic_size;

    ptr += message->topic_size;
    memory_offset += message->topic_size;

    if (message->qos > AT_MOST_ONCE) {
        if (buffer_read_struct(buf, "H", &message->id) != sizeof(uint16))
            return MQTT_DECODE_ERROR;
        consumed += sizeof(uint16);
    }

    usize properties_length = 0;
    int prop_bytes          = mqtt_variable_length_read(buf, &properties_length);
    if (prop_bytes < 0)
        return MQTT_DECODE_ERROR;
    consumed += prop_bytes;

    // Properties
    usize property_id         = 0;
    Publish_Properties *props = find_free_property_slot(ctx, &property_id);
    if (mqtt_publish_properties_read(buf, props, properties_length) != properties_length)
        return MQTT_DECODE_ERROR;
    consumed += properties_length;

    message->property_id    = property_id;
    message->message_offset = memory_offset;
    message->message_size   = header.remaining_length - consumed;

    if (message->message_size > 0) {
        if (buffer_read_binary(ptr, buf, message->message_size) != message->message_size)
            return MQTT_DECODE_ERROR;
        ptr += message->message_size;
        memory_offset += message->message_size;
        consumed += message->message_size;
    }

    // Validate total consumption
    if (consumed != header.remaining_length) {
        log_error("recv: PUBLISH packet length mismatch - consumed %zu, expected %zu", consumed,
                  header.remaining_length);
        return MQTT_DECODE_ERROR;
    }

    log_info("recv: PUBLISH id: %d, dup: %d, retain: %d  qos=%d", message->id, message->dup,
             message->retain, message->qos);

    return MQTT_DECODE_SUCCESS;
}

// Write properties to buffer
static isize mqtt_publish_properties_write(Buffer *buf, const Publish_Properties *props)
{
    isize bytes_written = 0;

    if (props->has_payload_format) {
        bytes_written += buffer_write_struct(buf, "BB", PUBLISH_PROP_PAYLOAD_FORMAT_INDICATOR,
                                             props->payload_format_indicator);
    }

    if (props->has_message_expiry) {
        bytes_written += buffer_write_struct(buf, "BI", PUBLISH_PROP_MESSAGE_EXPIRY_INTERVAL,
                                             props->message_expiry_interval);
    }

    if (props->has_content_type) {
        bytes_written += buffer_write_struct(buf, "B", PUBLISH_PROP_CONTENT_TYPE);
        bytes_written += buffer_write_binary(buf, props->content_type, props->content_type_len);
    }

    if (props->has_response_topic) {
        bytes_written += buffer_write_struct(buf, "B", PUBLISH_PROP_RESPONSE_TOPIC);
        bytes_written += buffer_write_binary(buf, props->response_topic, props->response_topic_len);
    }

    if (props->has_correlation_data) {
        bytes_written += buffer_write_struct(buf, "B", PUBLISH_PROP_CORRELATION_DATA);
        bytes_written +=
            buffer_write_binary(buf, props->correlation_data, props->correlation_data_len);
    }

    if (props->has_topic_alias) {
        bytes_written +=
            buffer_write_struct(buf, "BH", PUBLISH_PROP_TOPIC_ALIAS, props->topic_alias);
    }

    // Write Subscription Identifiers
    for (int i = 0; i < props->subscription_id_count; i++) {
        bytes_written += buffer_write_struct(buf, "B", PUBLISH_PROP_SUBSCRIPTION_IDENTIFIER);
        bytes_written += mqtt_variable_length_write(buf, props->subscription_ids[i]);
    }

    return bytes_written;
}

static void mqtt_publish_single_write(Tera_Context *ctx, Message_Data *message)
{
    isize written_bytes       = 0;
    Publish_Properties *props = &ctx->properties_data[message->property_id];
    const char *publish_topic = (const char *)tera_message_data_buffer_at(message->topic_offset);

    for (usize i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
        if (!ctx->subscription_data[i].active)
            continue;

        Subscription_Data *subscription_data = &ctx->subscription_data[i];
        const char *topic =
            (const char *)tera_topic_data_buffer_at(subscription_data->topic_offset);

        if (message->topic_size == subscription_data->topic_size &&
            strncmp(topic, publish_topic, message->topic_size) == 0) {

            Connection_Data *cdata = &ctx->connection_data[subscription_data->client_id];
            buffer_reset(&cdata->send_buffer);

            // Remaining Variable Length
            // - len of topic uint16
            // - packet id uint16
            // - properties length
            // - topic size in bytes
            // - message size in bytes
            Fixed_Header header = {.bits.qos         = 0,
                                   .bits.dup         = 0,
                                   .bits.retain      = 0,
                                   .bits.type        = PUBLISH,
                                   .remaining_length = sizeof(uint16) + message->topic_size +
                                                       message->message_size};

            if (header.bits.qos > AT_MOST_ONCE)
                header.remaining_length += sizeof(uint16);

            props->subscription_ids[props->subscription_id_count++] = subscription_data->id;
            uint32 properties_length = calculate_publish_properties_length(props);
            header.remaining_length += mqtt_variable_length_encoded_length(properties_length);
            header.remaining_length += properties_length;

            isize fixed_header_len = mqtt_fixed_header_write(&cdata->send_buffer, &header);
            if (fixed_header_len < 0) {
                log_error("Failed to write packet header");
            } else {
                written_bytes += fixed_header_len;
            }

            // Topic Name
            written_bytes += buffer_write_binary(&cdata->send_buffer, topic, message->topic_size);

            // Packet identifier
            if (header.bits.qos > AT_MOST_ONCE)
                written_bytes += buffer_write_struct(&cdata->send_buffer, "H", message->id);

            // Properties
            written_bytes += mqtt_variable_length_write(&cdata->send_buffer, properties_length);
            written_bytes += mqtt_publish_properties_write(&cdata->send_buffer, props);

            // Payload
            uint8 *payload = tera_message_data_buffer_at(message->message_offset);
            if (message->message_size > 0)
                written_bytes +=
                    buffer_write_binary(&cdata->send_buffer, payload, message->message_size);

            log_info("sent: PUBLISH id: %d cid: %d sid: %d (o%i) (%li bytes)", message->id,
                     subscription_data->client_id, subscription_data->id,
                     subscription_data->options, written_bytes);

            written_bytes = 0;
        }
    }

    if (message->qos == AT_MOST_ONCE)
        message->active = false;
}

void mqtt_publish_write(Tera_Context *ctx)
{
    for (usize i = 0; i < MAX_PACKETS; ++i) {
        if (!ctx->message_data[i].active)
            continue;

        mqtt_publish_single_write(ctx, &ctx->message_data[i]);
    }
}
