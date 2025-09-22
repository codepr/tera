#include "mqtt.h"
#include "tera_internal.h"
#include <string.h>

static int mqtt_read_variable_byte_integer(Buffer *buf, uint32 *value)
{
    uint8_t byte;
    uint32_t result     = 0;
    uint32_t multiplier = 1;
    int bytes_read      = 0;

    *value              = 0;

    do {
        // Check buffer bounds
        if (buf->read_pos >= buf->size) {
            return -1;
        }

        // Check MQTT specification limit (max 4 bytes)
        if (bytes_read >= 4) {
            return -1;
        }

        byte = buf->data[buf->read_pos];
        buf->read_pos++;
        bytes_read++;

        result += (byte & 0x7F) * multiplier;

        // Check for overflow before next iteration
        if (multiplier > (UINT32_MAX / 128)) {
            return -1;
        }

        multiplier *= 128;

    } while ((byte & 0x80) != 0);

    // Validate minimum encoding (optional but recommended)
    // This ensures the value couldn't be encoded in fewer bytes
    if (bytes_read > 1) {
        uint32_t min_value_for_bytes = 1;
        for (int i = 1; i < bytes_read; i++) {
            min_value_for_bytes *= 128;
        }
        if (result < min_value_for_bytes) {
            return -1; // Not minimum encoding
        }
    }

    *value = result;
    return bytes_read;
}

/*
 *
 * Decode Remaining Length comprised of Variable Header and Payload if
 * present. It does not take into account the bytes for storing length.
 *
 * - Maximum of 4 bytes length
 * - Each byte encodes 7 bits of data + 1 continuation bit
 * - Maximum value is 268 435 455
 * - Uses the minimum amount of bytes necessary to represent
 */
isize mqtt_variable_length_read(Buffer *buf, usize *len)
{
    uint32_t value;
    int result = mqtt_read_variable_byte_integer(buf, &value);

    if (result < 0) {
        *len = 0;
        return 0; // Error - could also return -1 to indicate error
    }

    *len = (usize)value;
    return (isize)result;
}

/*
 * Encoding packet length function, follows the OASIS specs, encode the total
 * length of the packet excluding header and the space for the encoding itself
 * into a 1-4 bytes using the continuation bit technique to allow a dynamic
 * storing size:
 * Using the first 7 bits of a byte we can store values till 127 and use the
 * last bit as a switch to notify if the subsequent byte is used to store
 * remaining length or not.
 * Returns the number of bytes used to store the value passed.
 */
isize mqtt_variable_length_write(Buffer *buf, usize len)
{
    isize bytes    = 0;
    uint16 encoded = 0;

    do {
        if ((buf->write_pos - buf->read_pos) + 1 > MAX_VARIABLE_LENGTH_BYTES)
            return bytes;

        // Check buffer bounds
        if (buf->write_pos >= buf->size) {
            return -1;
        }

        encoded = len % 128;
        len /= 128;

        /* if there are more digits to encode, set the top bit of this digit
         */
        if (len > 0)
            encoded |= 128;

        *(buf->data + buf->write_pos) = encoded;
        buf->write_pos++;
        bytes++;
    } while (len > 0);

    return bytes;
}

static inline uint32 make_key(uint16 client_id, uint16 mid)
{
    uint32 hash_key = (client_id << 16) | mid;
    uint32 index    = (hash_key * 2654435761U) >> 19;
    return index;
}

Message_Delivery *mqtt_message_delivery_find_free(Tera_Context *ctx, uint16 *delivery_id)
{
    if (ctx->message_delivery_free_list_head == -1)
        return NULL;

    int16 index                           = ctx->message_delivery_free_list_head;
    ctx->message_delivery_free_list_head  = ctx->message_deliveries[index].next_free;

    ctx->message_deliveries[index].active = true;
    *delivery_id                          = index;

    return &ctx->message_deliveries[index];
}

Message_Delivery *mqtt_message_delivery_find_existing(Tera_Context *ctx, uint16 client_id,
                                                      uint16 mid)
{
    int16 key              = make_key(client_id, mid);
    Delivery_Bucket bucket = ctx->message_delivery_lookup_table[key];
    if (bucket.count == 0)
        return NULL;

    for (int8 i = 0; i < bucket.count; ++i) {
        Message_Delivery *delivery = &ctx->message_deliveries[bucket.indexes[i]];

        if (delivery->client_id == client_id && delivery->message_id == mid)
            return delivery;
    }

    return NULL;
}

void mqtt_message_delivery_add(Tera_Context *ctx, uint16 client_id, uint16 mid, uint16 index)
{
    int16 key                        = make_key(client_id, mid);
    Delivery_Bucket *bucket          = &ctx->message_delivery_lookup_table[key];

    bucket->indexes[bucket->count++] = index;
}

void mqtt_message_delivery_free(Tera_Context *ctx, uint16 client_id, uint16 mid)
{
    int16 key               = make_key(client_id, mid);
    Delivery_Bucket *bucket = &ctx->message_delivery_lookup_table[key];

    int8 index              = 0;
    for (int8 i = 0; i < bucket->count; ++i) {
        Message_Delivery *target = &ctx->message_deliveries[bucket->indexes[i]];

        if (target->client_id == client_id && target->message_id == mid) {
            index = i;
            break;
        }
    }

    int16 delivery_id = bucket->indexes[index];

    if (index < bucket->count - 1)
        memmove(bucket->indexes + index, bucket->indexes + index + 1,
                sizeof(int16) * bucket->count - index);
    else
        bucket->indexes[index] = -1;

    bucket->count--;

    ctx->message_deliveries[delivery_id].active    = false;

    // The released slot becomes the new head of the free list
    ctx->message_deliveries[delivery_id].next_free = ctx->message_delivery_free_list_head;
    ctx->message_delivery_free_list_head           = delivery_id;
}

Publish_Properties *mqtt_publish_properties_find_free(Tera_Context *ctx, int16 *property_id)
{
    if (ctx->property_free_list_head == -1)
        return NULL;

    int16 index                        = ctx->property_free_list_head;
    ctx->property_free_list_head       = ctx->properties_data[index].next_free;

    ctx->properties_data[index].active = true;
    *property_id                       = index;

    return &ctx->properties_data[index];
}

void mqtt_publish_properties_free(Tera_Context *ctx, int16 property_id)
{
    if (property_id >= MAX_PUBLISHED_MESSAGES)
        return;

    ctx->properties_data[property_id].active    = false;

    // The released slot becomes the new head of the free list
    ctx->properties_data[property_id].next_free = ctx->property_free_list_head;
    ctx->property_free_list_head                = property_id;
}

Published_Message *mqtt_published_message_find_free(Tera_Context *ctx, uint16 *published_id)
{
    if (ctx->published_free_list_head == -1)
        return NULL;

    int16 index                            = ctx->published_free_list_head;
    ctx->published_free_list_head          = ctx->published_messages[index].next_free;

    Data_Flags flags                       = data_flags_set(false, 0, false, true);
    ctx->published_messages[index].options = flags.value;

    *published_id                          = index;

    return &ctx->published_messages[index];
}

void mqtt_published_message_free(Tera_Context *ctx, uint16 published_id)
{
    if (published_id >= MAX_PUBLISHED_MESSAGES)
        return;

    ctx->published_messages[published_id].deliveries--;

    if (ctx->published_messages[published_id].deliveries == 0) {
        ctx->published_messages[published_id].options =
            data_flags_active_set(ctx->published_messages[published_id].options, 0);

        mqtt_publish_properties_free(ctx, ctx->published_messages[published_id].property_id);

        // The released slot becomes the new head of the free list
        ctx->published_messages[published_id].next_free = ctx->published_free_list_head;
        ctx->published_free_list_head                   = published_id;
    }
}

void mqtt_message_dump(const Buffer *buf, bool read)
{
    int limit = read ? buf->read_pos : buf->write_pos;
    log_info("Packet dump: ");
    for (int i = 0; i < limit; i++) {
        printf("%02x ", buf->data[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    printf("\n");
}
