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
static uint8 client_data_buffer[MAX_CLIENT_DATA_BUFFER_SIZE]   = {0};
static Arena client_arena                                      = {0};

static uint8 message_data_buffer[MAX_MESSAGE_DATA_BUFFER_SIZE] = {0};
static Arena message_arena                                     = {0};

static uint8 topic_data_buffer[MAX_TOPIC_DATA_BUFFER_SIZE]     = {0};
static Arena topic_arena                                       = {0};

static uint8 io_buffer[MAX_MESSAGE_DATA_BUFFER_SIZE]           = {0};
static Arena io_arena                                          = {0};

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
    bool clean_session;

    // Byte string sizes in memory
    uint8 client_id_size;
    uint8 username_size;
    uint8 password_size;
    uint8 will_topic_size;
    uint8 will_message_size;
} Client_Data;

typedef struct connection_data {
    int socket_fd;
    Buffer recv_buffer;
    Buffer send_buffer;
} Connection_Data;

typedef uint32 Message_ID;
typedef struct tera_context {
    Arena *io_arena;
    Arena *client_arena;
    Arena *topic_arena;
    Arena *message_arena;
    Connection_Data connection_data[MAX_CLIENTS];
    Client_Data client_data[MAX_CLIENTS];
    Message_Data message_data[MAX_PACKETS];
    Publish_Properties properties_data[MAX_PACKETS];
    Subscription_Data subscription_data[MAX_SUBSCRIPTIONS];
} Tera_Context;

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

    for (usize i = 0; i < MAX_SUBSCRIPTIONS; ++i)
        ctx->subscription_data[i].active = false;

    for (usize i = 0; i < MAX_PACKETS; ++i)
        ctx->message_data[i].active = false;
}

static inline uint8 *tera_topic_data_buffer_at(usize index) { return &topic_data_buffer[index]; }
static inline uint8 *tera_message_data_buffer_at(usize index)
{
    return &message_data_buffer[index];
}
