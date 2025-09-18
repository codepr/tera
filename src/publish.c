#include "logger.h"
#include "mqtt.h"
#include "tera_internal.h"
#include <string.h>

static Published_Message *find_free_message_slot(Tera_Context *ctx)
{
    Published_Message *published_msg = NULL;
    for (usize i = 0; i < MAX_PUBLISHED_MESSAGES; ++i) {
        published_msg = &ctx->published_messages[i];
        if (data_flags_active_get(published_msg->options))
            continue;

        Data_Flags flags       = data_flags_set(false, 0, false, true);
        published_msg->options = flags.value;

        break;
    }

    return published_msg;
}

static Publish_Properties *find_free_property_slot(Tera_Context *ctx, usize *property_id)
{
    // TODO make this useful (id recycling etc etc)
    for (usize i = 0; i < MAX_PUBLISHED_MESSAGES; ++i) {
        if (ctx->properties_data[i].active)
            continue;

        ctx->properties_data[i].active = true;
        *property_id                   = i;
        return &ctx->properties_data[i];
    }

    return NULL;
}

static Message_Delivery *find_free_delivery_slot(Tera_Context *ctx, int16 client_id, uint16 mid)
{
    Message_Delivery *delivery = NULL;
    for (usize i = 0; i < MAX_DELIVERY_MESSAGES; ++i) {
        delivery = &ctx->message_deliveries[i];

        if (delivery->active && delivery->client_id == client_id && delivery->published_msg_id)
            return delivery;

        if (delivery->active)
            continue;

        break;
    }

    return delivery;
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
        length += sizeof(uint8) + sizeof(uint16) +
                  props->content_type_len; // Property ID + length + string
    }

    if (props->has_response_topic) {
        length += sizeof(uint8) + sizeof(uint16) +
                  props->response_topic_len; // Property ID + length + string
    }

    if (props->has_correlation_data) {
        length += sizeof(uint8) + sizeof(uint16) +
                  props->correlation_data_len; // Property ID + length + data
    }

    if (props->has_topic_alias) {
        length += sizeof(uint8) + sizeof(uint16); // Property ID + 2 byte value
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

    Published_Message *message = find_free_message_slot(ctx);
    Data_Flags flags = data_flags_set(header.bits.retain, header.bits.qos, header.bits.dup, true);
    message->options = flags.value;

    message->topic_offset = arena_current_offset(ctx->message_arena);
    if (buffer_read_struct(buf, "H", &message->topic_size) != sizeof(uint16))
        return MQTT_DECODE_ERROR;

    consumed += sizeof(uint16);

    uint8 *ptr = arena_alloc(ctx->message_arena, message->topic_size);
    if (!ptr) {
        // TODO handle case
        log_critical("bump arena OOM");
    }

    if (buffer_read_binary(ptr, buf, message->topic_size) != message->topic_size)
        return MQTT_DECODE_ERROR;

    consumed += message->topic_size;

    if (header.bits.qos > AT_MOST_ONCE) {
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
    if (mqtt_publish_properties_read(buf, props, properties_length) != MQTT_DECODE_SUCCESS)
        return MQTT_DECODE_ERROR;
    consumed += properties_length;

    message->property_id    = property_id;
    message->message_offset = arena_current_offset(ctx->message_arena);
    message->message_size   = header.remaining_length - consumed;

    ptr                     = arena_alloc(ctx->message_arena, message->message_size);
    if (!ptr) {
        // TODO handle case
        log_critical("bump arena OOM");
    }

    if (message->message_size > 0) {
        if (buffer_read_binary(ptr, buf, message->message_size) != message->message_size)
            return MQTT_DECODE_ERROR;
        consumed += message->message_size;
    }

    // Validate total consumption
    if (consumed != header.remaining_length) {
        log_error("recv: PUBLISH packet length mismatch - consumed %zu, expected %zu", consumed,
                  header.remaining_length);
        return MQTT_DECODE_ERROR;
    }

    log_info("recv: PUBLISH id: %d, dup: %d, retain: %d  qos: %d", message->id, header.bits.dup,
             header.bits.retain, header.bits.qos);

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
        bytes_written +=
            buffer_write_utf8_string(buf, props->content_type, props->content_type_len);
    }

    if (props->has_response_topic) {
        bytes_written += buffer_write_struct(buf, "B", PUBLISH_PROP_RESPONSE_TOPIC);
        bytes_written +=
            buffer_write_utf8_string(buf, props->response_topic, props->response_topic_len);
    }

    if (props->has_correlation_data) {
        bytes_written += buffer_write_struct(buf, "B", PUBLISH_PROP_CORRELATION_DATA);
        bytes_written +=
            buffer_write_utf8_string(buf, props->correlation_data, props->correlation_data_len);
    }

    if (props->has_topic_alias) {
        bytes_written +=
            buffer_write_struct(buf, "BH", PUBLISH_PROP_TOPIC_ALIAS, props->topic_alias);
    }

    if (props->subscription_id_count == 0)
        bytes_written += buffer_write_struct(buf, "B", 0);

    // Write Subscription Identifiers
    for (int i = 0; i < props->subscription_id_count; i++) {
        bytes_written += buffer_write_struct(buf, "B", PUBLISH_PROP_SUBSCRIPTION_IDENTIFIER);
        bytes_written += mqtt_variable_length_write(buf, props->subscription_ids[i]);
    }

    return bytes_written;
}

static int publish_properties_add_subscription(Publish_Properties *props, int16 subscription_id)
{
    if (subscription_id < 0)
        return 0;
    if (props->subscription_id_count >= MAX_SUBSCRIPTION_IDS)
        return -1;
    for (int i = 0; i < props->subscription_id_count; i++)
        if (props->subscription_ids[i] == subscription_id)
            return 0;

    props->subscription_ids[props->subscription_id_count++] = subscription_id;
    return 0;
}

static bool topic_matches(const Tera_Context *ctx, const char *topic, usize topic_size,
                          const Subscription_Data *subdata)
{
    const char *sub_topic = (const char *)arena_at(ctx->topic_arena, subdata->topic_offset);

    return (topic_size == subdata->topic_size && strncmp(topic, sub_topic, topic_size) == 0);
}

static void mqtt_publish_fanout(Tera_Context *ctx, uint16 index, const Client_Data *cdata)
{
    Published_Message *pub_msg = &ctx->published_messages[index];
    const char *publish_topic  = (const char *)arena_at(ctx->message_arena, pub_msg->topic_offset);
    isize written_bytes        = 0;
    Publish_Properties *props  = &ctx->properties_data[pub_msg->property_id];
    const uint8 *payload       = arena_at(ctx->message_arena, pub_msg->message_offset);
    Buffer *buf                = NULL;
    Data_Flags message_flags   = data_flags_get(pub_msg->options);

    for (usize i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
        if (!ctx->subscription_data[i].active)
            continue;

        Subscription_Data *subscription_data = &ctx->subscription_data[i];
        if (!topic_matches(ctx, publish_topic, pub_msg->topic_size, subscription_data))
            continue;

        // Create delivery record for this subscription
        Message_Delivery *delivery =
            find_free_delivery_slot(ctx, subscription_data->client_id, pub_msg->id);
        if (!delivery)
            continue;

        // There is already a delivery in progress for this subscription
        if (delivery->active)
            continue;

        delivery->published_msg_id = pub_msg->id;
        delivery->client_id        = subscription_data->client_id;
        delivery->message_id       = mqtt_subscription_next_mid(subscription_data);

        /*
         * Update QoS according to subscriber's one, following MQTT
         * rules: The min between the original QoS and the subscriber
         * QoS
         */
        uint8 granted_qos          = subscription_data->options & 0x03;
        delivery->delivery_qos =
            message_flags.bits.qos >= granted_qos ? granted_qos : message_flags.bits.qos;
        delivery->state        = (delivery->delivery_qos == AT_MOST_ONCE)    ? MSG_ACKNOWLEDGED
                                 : (delivery->delivery_qos == AT_LEAST_ONCE) ? MSG_AWAITING_PUBACK
                                                                             : MSG_AWAITING_PUBREC;
        delivery->last_sent_at = current_micros();
        delivery->next_retry_at =
            (delivery->delivery_qos > AT_MOST_ONCE) ? delivery->last_sent_at + RETRY_TIMEOUT_MS : 0;
        delivery->retry_count = 0;
        delivery->active      = (delivery->delivery_qos != AT_MOST_ONCE);

        // Write to subscription buffer

        buf                   = &ctx->connection_data[subscription_data->client_id].send_buffer;
        buffer_reset(buf);

        // Remaining Variable Length
        // - len of topic uint16
        // - packet id uint16
        // - properties length
        // - topic size in bytes
        // - message size in bytes
        Fixed_Header header = {.bits.qos    = delivery->delivery_qos,
                               .bits.dup    = 0,
                               .bits.retain = 0,
                               .bits.type   = PUBLISH,
                               .remaining_length =
                                   sizeof(uint16) + pub_msg->topic_size + pub_msg->message_size};

        if (header.bits.qos > AT_MOST_ONCE)
            header.remaining_length += sizeof(uint16);

        publish_properties_add_subscription(props, subscription_data->id);
        uint32 properties_length = calculate_publish_properties_length(props);
        header.remaining_length += mqtt_variable_length_encoded_length(properties_length);
        header.remaining_length += properties_length;

        isize fixed_header_len = mqtt_fixed_header_write(buf, &header);
        if (fixed_header_len < 0) {
            log_error(">>>>: Failed to write packet header");
        } else {
            written_bytes += fixed_header_len;

            // Topic Name
            written_bytes += buffer_write_utf8_string(buf, publish_topic, pub_msg->topic_size);

            // Packet identifier
            if (header.bits.qos > AT_MOST_ONCE) {
                written_bytes += buffer_write_struct(buf, "H", delivery->message_id);
            }

            // Properties
            written_bytes += mqtt_variable_length_write(buf, properties_length);
            written_bytes += mqtt_publish_properties_write(buf, props);

            // Payload
            if (pub_msg->message_size > 0)
                written_bytes += buffer_write_binary(buf, payload, pub_msg->message_size);

            log_info("sent: PUBLISH id: %d cid: %d sid: %d qos: %d (%li bytes)",
                     delivery->message_id, subscription_data->client_id, subscription_data->id,
                     delivery->delivery_qos, written_bytes);
        }

        written_bytes = 0;
    }

    // TODO not great to do this here
    switch (message_flags.bits.qos) {
    case AT_MOST_ONCE:
        pub_msg->options = data_flags_active_set(pub_msg->options, 0);
        break;
    case AT_LEAST_ONCE:
        // Check if one exits already first
        Message_Delivery *delivery = find_free_delivery_slot(ctx, cdata->conn_id, pub_msg->id);
        if (delivery->active)
            break;

        mqtt_ack_write(ctx, cdata, PUBACK, pub_msg->id);
        pub_msg->options = data_flags_active_set(pub_msg->options, 0);
        break;
    case EXACTLY_ONCE: {
        // Create delivery record for this subscription, check if one exits already first
        Message_Delivery *delivery = find_free_delivery_slot(ctx, cdata->conn_id, pub_msg->id);
        if (delivery->active)
            break;

        mqtt_ack_write(ctx, cdata, PUBREC, pub_msg->id);

        delivery->published_msg_id = pub_msg->id;
        delivery->client_id        = cdata->conn_id;
        delivery->message_id       = pub_msg->id;

        /*
         * Update QoS according to publisher's one, following MQTT
         * rules: The min between the original QoS and the subscriber
         * QoS
         */
        delivery->delivery_qos     = message_flags.bits.qos;
        delivery->state            = MSG_AWAITING_PUBREL;
        delivery->last_sent_at     = current_micros();
        delivery->next_retry_at    = delivery->last_sent_at + RETRY_TIMEOUT_MS;
        delivery->retry_count      = 0;
        delivery->active           = true;

        break;
    }
    }
}

void mqtt_publish_write(Tera_Context *ctx, const Client_Data *cdata)
{
    for (usize i = 0; i < MAX_PUBLISHED_MESSAGES; ++i) {
        if (!data_flags_active_get(ctx->published_messages[i].options))
            continue;

        mqtt_publish_fanout(ctx, i, cdata);
    }
}

void mqtt_publish_retry(Tera_Context *ctx, Message_Delivery *delivery)
{
    Published_Message *pub_msg = &ctx->published_messages[delivery->published_msg_id];
    Buffer *buf                = &ctx->connection_data[delivery->client_id].send_buffer;
    Publish_Properties *props  = &ctx->properties_data[pub_msg->property_id];
    const uint8 *payload       = arena_at(ctx->message_arena, pub_msg->message_offset);
    const char *publish_topic  = (const char *)arena_at(ctx->message_arena, pub_msg->topic_offset);
    isize written_bytes        = 0;
    buffer_reset(buf);

    // Remaining Variable Length
    // - len of topic uint16
    // - packet id uint16
    // - properties length
    // - topic size in bytes
    // - message size in bytes
    Fixed_Header header = {.bits.qos    = delivery->delivery_qos,
                           .bits.dup    = 0,
                           .bits.retain = 0,
                           .bits.type   = PUBLISH,
                           .remaining_length =
                               sizeof(uint16) + pub_msg->topic_size + pub_msg->message_size};

    if (header.bits.qos > AT_MOST_ONCE)
        header.remaining_length += sizeof(uint16);

    uint32 properties_length = calculate_publish_properties_length(props);
    header.remaining_length += mqtt_variable_length_encoded_length(properties_length);
    header.remaining_length += properties_length;

    isize fixed_header_len = mqtt_fixed_header_write(buf, &header);
    if (fixed_header_len < 0) {
        log_error(">>>>: Failed to write packet header");
    } else {
        written_bytes += fixed_header_len;

        // Topic Name
        written_bytes += buffer_write_utf8_string(buf, publish_topic, pub_msg->topic_size);

        // Packet identifier
        if (header.bits.qos > AT_MOST_ONCE)
            written_bytes += buffer_write_struct(buf, "H", delivery->message_id);

        // Properties
        written_bytes += mqtt_variable_length_write(buf, properties_length);
        written_bytes += mqtt_publish_properties_write(buf, props);

        // Payload
        if (pub_msg->message_size > 0)
            written_bytes += buffer_write_binary(buf, payload, pub_msg->message_size);

        log_info("sent: PUBLISH id: %d cid: %d qos: %d (%li bytes)", delivery->message_id,
                 delivery->client_id, delivery->delivery_qos, written_bytes);
    }
}
