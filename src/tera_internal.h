#pragma once

#include "buffer.h"
#include "mqtt.h"
#include "types.h"

#define MAX_CLIENTS                  1024
#define MAX_CLIENT_SIZE              1024
#define MAX_PACKET_SIZE              1024
#define MAX_PUBLISHED_MESSAGES       1024
#define MAX_DELIVERY_MESSAGES        (8 * MAX_PUBLISHED_MESSAGES)

#define MAX_CLIENT_DATA_BUFFER_SIZE  (MAX_CLIENTS * MAX_CLIENT_SIZE)
#define MAX_MESSAGE_DATA_BUFFER_SIZE (MAX_DELIVERY_MESSAGES * MAX_PACKET_SIZE)

#define MAX_SUBSCRIPTIONS            8192
#define MAX_TOPIC_DATA_BUFFER_SIZE   (MAX_SUBSCRIPTIONS) * 64

#define MQTT_RETRANSMISSION_CHECK_MS 5000
#define MQTT_MAX_RETRY_ATTEMPTS      5
#define MQTT_RETRY_TIMEOUT_MS        20000

// Main pools of pre-allocated data
// TODO use a bump allocator on heap
extern uint8 client_data_buffer[];
extern Arena client_arena;

extern uint8 message_data_buffer[];
extern Arena message_arena;

extern uint8 topic_data_buffer[];
extern Arena topic_arena;

extern uint8 io_buffer[];
extern Arena io_arena;

typedef struct client_data {
    // MQTT connect flags and ID
    uint16 client_id_offset;
    uint16 will_topic_offset;
    uint16 will_message_offset;
    uint16 username_offset;
    uint16 password_offset;

    // Connection data
    uint16 conn_id;
    uint16 keepalive;
    uint8 connect_flags;

    // Byte string sizes in memory
    uint8 client_id_size;
    uint8 username_size;
    uint8 password_size;
    uint8 will_topic_size;
    uint8 will_message_size;
} Client_Data;

/*
 * Wrapper structure around a connected client, each connection can be a publisher
 * or a subscriber.
 * As of now, no allocations will occur, just a big pool of memory at the
 * start of the application will serve us a client pool, read and write buffers
 * are initialized at the start.
 */
typedef struct connection_data {
    Buffer recv_buffer;
    Buffer send_buffer;
    int socket_fd;
    bool connected;
} Connection_Data;

/**
 * Main server context structure containing all global state for the MQTT broker.
 *
 * This structure centralizes all broker state to enable explicit dependency passing.
 * The design separates concerns into memory management (arenas)
 * and data storage (arrays).
 *
 * Memory Management:
 * - Uses separate arena allocators for different entity types to reduce fragmentation
 * - Each arena serves a specific purpose (I/O buffers, client data, topics, messages)
 *
 * Data Storage:
 * - Fixed-size arrays provide predictable memory usage and avoid dynamic allocation
 * - Arrays are sized by MAX_* constants to enforce broker capacity limits
 * - Parallel arrays (connection_data/client_data) allow separation of transport vs
 *   protocol state
 *
 * Memory Layout:
 * - Connection/client data: Per-socket state for active connections
 * - Published messages: Tracking QoS 1/2 messages awaiting acknowledgment
 * - Message deliveries: Retry logic and delivery state for reliable messaging, 1:N fanout
 *                       logic where each published message can have multiple active
 *                       deliveries depending on how many subscribers are connected to the
 *                       topic it publishes to
 * - Properties: MQTT 5.0 properties associated with published messages
 * - Subscriptions: Topic filters and associated client subscriptions, wildacards and
 *                  hierarchies are not supported as of yet
 */
typedef struct tera_context {
    // Memory arenas, separated by entity
    Arena *io_arena;
    Arena *client_arena;
    Arena *topic_arena;
    Arena *message_arena;

    // Data arrays
    Connection_Data connection_data[MAX_CLIENTS];
    Client_Data client_data[MAX_CLIENTS];
    Published_Message published_messages[MAX_PUBLISHED_MESSAGES];
    Message_Delivery message_deliveries[MAX_DELIVERY_MESSAGES];
    Publish_Properties properties_data[MAX_PUBLISHED_MESSAGES];
    Subscription_Data subscription_data[MAX_SUBSCRIPTIONS];
} Tera_Context;

/**
 * Simple helper structure to quickly access all the encoded bitfields
 * of each published message
 */
typedef union data_flags {
    uint8 value;
    struct {
        uint8 retain : 1;
        uint8 qos : 2;
        uint8 dup : 1;
        uint8 active : 1;
        uint8 reserved : 3;
    } bits;
} Data_Flags;

static inline Data_Flags data_flags_get(uint8 byte)
{
    return (Data_Flags){.bits = {.retain = (uint8)(((byte) >> 0) & 0x01),
                                 .qos    = (uint8)(((byte) >> 1) & 0x03),
                                 .dup    = (uint8)(((byte) >> 3) & 0x01),
                                 .active = (uint8)(((byte) >> 4) & 0x01)}};
}

static inline Data_Flags data_flags_set(bool retain, uint8 qos, bool dup, bool active)
{
    return (Data_Flags){.bits = {.retain = retain, .qos = qos, .dup = dup, .active = active}};
}

static inline uint8 data_flags_active_get(uint8 byte) { return (((byte) >> 4) & 0x01); }
static inline uint8 data_flags_active_set(uint8 byte, uint8 value)
{
    return (byte & ~(0x01 << 0x04)) | ((value & 0x01) << 0x04);
}

static inline void tera_context_init(Tera_Context *ctx)
{
    arena_init(&client_arena, client_data_buffer, MAX_CLIENT_DATA_BUFFER_SIZE);
    arena_init(&message_arena, message_data_buffer, MAX_MESSAGE_DATA_BUFFER_SIZE);
    arena_init(&topic_arena, topic_data_buffer, MAX_TOPIC_DATA_BUFFER_SIZE);
    arena_init(&io_arena, io_buffer, MAX_MESSAGE_DATA_BUFFER_SIZE);

    ctx->io_arena      = &io_arena;
    ctx->topic_arena   = &topic_arena;
    ctx->client_arena  = &client_arena;
    ctx->message_arena = &message_arena;

    for (usize i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
        ctx->subscription_data[i].active = false;
        ctx->subscription_data[i].mid    = 1;
    }

    for (usize i = 0; i < MAX_PUBLISHED_MESSAGES; ++i) {
        ctx->published_messages[i].options = 0;
    }

    for (usize i = 0; i < MAX_PUBLISHED_MESSAGES; ++i)
        ctx->properties_data[i].active = false;
}
