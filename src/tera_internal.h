#pragma once

#include "buffer.h"
#include "mqtt.h"
#include "types.h"

#define MAX_CLIENTS                  1024
#define MAX_CLIENT_SIZE              1024
#define MAX_PACKET_SIZE              1024
#define MAX_PACKETS                  4096

#define MAX_CLIENT_DATA_BUFFER_SIZE  (MAX_CLIENTS * MAX_CLIENT_SIZE)
#define MAX_MESSAGE_DATA_BUFFER_SIZE (MAX_PACKETS * MAX_PACKET_SIZE)

#define MAX_SUBSCRIPTIONS            8192
#define MAX_TOPIC_DATA_BUFFER_SIZE   (MAX_SUBSCRIPTIONS) * (MAX_PACKET_SIZE)

#define NO_DATA_OFFSET               0xFFFF // Using the max value as a sentinel

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
    uint16 username_offset; // Will be NO_DATA_OFFSET if no username
    uint16 password_offset; // Will be NO_DATA_OFFSET if no password

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

#define MAX_RETRY_ATTEMPTS 5
#define RETRY_TIMEOUT_MS   20000

// Message queue for retransmission logic
typedef struct retry_scheduler {
    int64 next_check_time;               // Earliest retry time
    uint16 pending_retries[MAX_PACKETS]; // Indices of messages needing retransmission
    uint16 retry_queue_head;
    uint16 retry_queue_tail;
} Retry_Scheduler;

typedef struct tera_context {
    // Memory arenas, separated by entity
    Arena *io_arena;
    Arena *client_arena;
    Arena *topic_arena;
    Arena *message_arena;

    // Data arrays
    Connection_Data connection_data[MAX_CLIENTS];
    Client_Data client_data[MAX_CLIENTS];
    Message_Data message_data[MAX_PACKETS];
    Publish_Properties properties_data[MAX_PACKETS];
    Subscription_Data subscription_data[MAX_SUBSCRIPTIONS];

    Retry_Scheduler retry_scheduler;
} Tera_Context;

typedef union data_flags {
    uint8 value;
    struct {
        uint8 retain : 1;
        uint8 qos : 2;
        uint8 dup : 1;
        uint8 active : 1;
        uint8 acknowledged : 1;
        uint8 reserved : 2;
    } bits;
} Data_Flags;

static inline Data_Flags data_flags_get(uint8 byte)
{
    return (Data_Flags){.bits = {.retain       = (uint8)(((byte) >> 0) & 0x01),
                                 .qos          = (uint8)(((byte) >> 1) & 0x03),
                                 .dup          = (uint8)(((byte) >> 3) & 0x01),
                                 .active       = (uint8)(((byte) >> 4) & 0x01),
                                 .acknowledged = (uint8)(((byte) >> 5) & 0x01)}};
}

static inline Data_Flags data_flags_set(bool retain, uint8 qos, bool dup, bool active,
                                        bool acknowledged)
{
    return (Data_Flags){.bits = {.retain       = retain,
                                 .qos          = qos,
                                 .dup          = dup,
                                 .active       = active,
                                 .acknowledged = acknowledged}};
}

static inline uint8 data_flags_active_get(uint8 byte) { return (((byte) >> 4) & 0x01); }
static inline uint8 data_flags_active_set(uint8 byte, uint8 value)
{
    return (byte & ~(0x01 << 0x04)) | ((value & 0x01) << 0x04);
}
static inline uint8 data_flags_acknowledged_get(uint8 byte) { return (((byte) >> 5) & 0x01); }
static inline uint8 data_flags_acknowledged_set(uint8 byte, uint8 value)
{
    return (byte & ~(0x05 << 0x01)) | ((value & 0x01) << 0x05);
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

    for (usize i = 0; i < MAX_PACKETS; ++i) {
        ctx->message_data[i].options = 0;
        ctx->message_data[i].options = data_flags_acknowledged_set(ctx->message_data[i].options, 1);
    }

    for (usize i = 0; i < MAX_PACKETS; ++i)
        ctx->properties_data[i].active = false;
}

static inline uint8 *tera_topic_data_buffer_at(usize index) { return &topic_data_buffer[index]; }
static inline uint8 *tera_message_data_buffer_at(usize index)
{
    return &message_data_buffer[index];
}
