#include "arena.h"
#include "buffer.h"
#include "iomux.h"
#include "logger.h"
#include "mqtt.h"
#include "net.h"
#include "tera_internal.h"
#include "types.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

// ============================ Globals ==============================

uint8 client_data_buffer[MAX_CLIENT_DATA_BUFFER_SIZE]   = {0};
Arena client_arena                                      = {0};

uint8 message_data_buffer[MAX_MESSAGE_DATA_BUFFER_SIZE] = {0};
Arena message_arena                                     = {0};

uint8 topic_data_buffer[MAX_TOPIC_DATA_BUFFER_SIZE]     = {0};
Arena topic_arena                                       = {0};

uint8 io_buffer[MAX_MESSAGE_DATA_BUFFER_SIZE]           = {0};
Arena io_arena                                          = {0};

typedef enum {
    TRANSPORT_SUCCESS           = 0,
    TRANSPORT_EAGAIN            = 0,
    TRANSPORT_DISCONNECT        = -1,
    TRANSPORT_INCOMPLETE_PACKET = -2,
} Transport_Result;

// Global context of the server, this will be passed around anywhere
// the global state is accessed or mutated
static Tera_Context context = {0};

// ======================== Static helpers ===========================

/**
 * This function is pretty simple, just loop through all the existing
 * connected clients and ensure that the out buffer is flushed.
 */
static void broadcast_replies(Tera_Context *ctx)
{
    Connection_Data *cd = NULL;
    for (usize i = 0; i < MAX_CLIENTS; ++i) {
        cd = &ctx->connection_data[i];
        if (!cd->connected)
            continue;
        if (buffer_is_empty(&cd->send_buffer))
            continue;

        buffer_net_send(&cd->send_buffer, cd->socket_fd);
    }
}

/**
 * When a PUBLISH message is received, we first need to find a metadata
 * slot in the published messages table, then we can proceed at decoding
 * it from the raw binary payload.
 */
static Published_Message *find_free_published_message(Tera_Context *ctx)
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

static void free_subscriptions(Tera_Context *ctx, Client_Data *client)
{
    for (usize i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
        if (!ctx->subscription_data[i].active)
            continue;

        if (ctx->subscription_data[i].client_id == client->conn_id)
            ctx->subscription_data[i].active = false;
    }
}

/**
 * Once a published message have concluded its lifecycle, e.g.
 * - A PUBLISH message that must be acknowledged by the publisher
 * - A PUBLISH message that must be akcnowledged by 1 or more subscribers,
 *   depending on the QoS level
 *
 * It's memory slot for metadata will be free to be re-used by another
 * incoming message. This function sets the message slot as free using
 * the message ID to find the position in the array.
 */
static void free_published_message(Tera_Context *ctx, int16 mid)
{
    // TODO can use a more efficient way then linear scan
    for (usize i = 0; i < MAX_PUBLISHED_MESSAGES; ++i) {
        if (!data_flags_active_get(ctx->published_messages[i].options))
            continue;

        if (ctx->published_messages[i].id == mid) {
            ctx->published_messages[i].options =
                data_flags_active_set(ctx->published_messages[i].options, 0);
            ctx->properties_data[ctx->published_messages[i].property_id].active = false;
        }
    }
}

/**
 * Iterate through the published messages to ensure that they have
 * been correctly delivered
 */
static void process_delivery_timeouts(Tera_Context *ctx, int64 current_time)
{
    for (usize i = 0; i < MAX_DELIVERY_MESSAGES; ++i) {
        Message_Delivery *delivery = &ctx->message_deliveries[i];

        if (delivery->state == MSG_ACKNOWLEDGED || delivery->state == MSG_EXPIRED)
            continue;

        if (delivery->next_retry_at > 0 && current_time >= delivery->next_retry_at) {
            if (delivery->retry_count >= MAX_RETRY_ATTEMPTS) {
                delivery->state  = MSG_EXPIRED;
                delivery->active = false;
                free_published_message(ctx, delivery->published_msg_id);
            } else {
                delivery->retry_count++;
                delivery->last_sent_at  = current_time;
                delivery->next_retry_at = current_time + RETRY_TIMEOUT_MS;
                mqtt_publish_retry(ctx, delivery);
            }
        }
    }
    broadcast_replies(ctx);
}

/**
 * Intermediate or final steps of each delivery to ensure that each subscriber
 * or publisher has been acknowledged, eventually leading to a publised message
 * to be freed.
 */
static void update_message_delivery(Tera_Context *ctx, uint16 client_id, int16 mid,
                                    Delivery_State new_state)
{
    for (usize i = 0; i < MAX_DELIVERY_MESSAGES; ++i) {
        Message_Delivery *delivery = &ctx->message_deliveries[i];
        if (!delivery->active)
            continue;

        if (delivery->client_id == client_id && delivery->message_id == mid) {
            delivery->state = new_state;

            if (new_state == MSG_ACKNOWLEDGED) {
                delivery->active = false;
                free_published_message(ctx, delivery->published_msg_id);
            }
            break;
        }
    }
}

static Transport_Result process_client_messages(Tera_Context *ctx, int fd)
{
    Client_Data *client    = &ctx->client_data[fd];
    Connection_Data *cdata = &ctx->connection_data[fd];

    buffer_reset(&cdata->recv_buffer);
    isize nread = buffer_net_recv(&cdata->recv_buffer, cdata->socket_fd);
    if (nread < 0) {
        // No data available right now
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /*
             * We have an EAGAIN error, which is really just signaling that
             * for some reasons the kernel is not ready to read more bytes at
             * the moment and it would block, so we just want to re-try some
             * time later, re-enqueuing a new read event
             */
            return TRANSPORT_EAGAIN;
        }
        /*
         * We got an unexpected error or a disconnection from the
         * client side, remove client from the global map and
         * free resources allocated such as io_event structure and
         * paired payload
         */

        // TODO Handle client disconnection here
        return TRANSPORT_DISCONNECT;
    }

    Buffer *buf = &cdata->recv_buffer;

    while (!buffer_is_empty(buf)) {

        MQTT_Decode_Result result = MQTT_DECODE_SUCCESS;

        // Check if we have at least fixed header (2 bytes minimum)
        if (buffer_available(buf) < 2) {
            log_debug(">>>>: Incomplete packet - need more data");
            return TRANSPORT_INCOMPLETE_PACKET;
        }

        /*
         * MQTT Fixed header, according to official docs it's comprised of a single
         * byte carrying:
         * - opcode (packet type)
         * - dup flag
         * - QoS
         * - retain flag
         * It's followed by the remaining_len of the packet, encoded onto 1 to 4
         * bytes starting at bytes 2.
         *
         * |   Bit      |  7  |  6  |  5  |  4  |  3  |  2  |  1  |   0    |
         * |------------|-----------------------|--------------------------|
         * | Byte 1     |      MQTT type 3      | dup |    QoS    | retain |
         * |------------|--------------------------------------------------|
         * | Byte 2     |                                                  |
         * |   .        |               Remaining Length                   |
         * |   .        |                                                  |
         * | Byte 5     |                                                  |
         * |------------|--------------------------------------------------|
         */
        uint8 header = *(cdata->recv_buffer.data + cdata->recv_buffer.read_pos);

        switch (mqtt_type_get(header)) {
        case CONNECT:
            result = mqtt_connect_read(ctx, client);
            if (result == MQTT_DECODE_SUCCESS)
                mqtt_connack_write(ctx, client, CONNACK_SUCCESS);
            else if (result == MQTT_DECODE_INVALID)
                return TRANSPORT_DISCONNECT;
            break;
        case DISCONNECT:
            result = mqtt_disconnect_read(ctx, client);
            if (result == MQTT_DECODE_SUCCESS)
                free_subscriptions(ctx, client);
            return TRANSPORT_DISCONNECT;
        case SUBSCRIBE: {
            Subscribe_Result sub_result = {0};
            result                      = mqtt_subscribe_read(ctx, client, &sub_result);
            if (result == MQTT_DECODE_SUCCESS)
                mqtt_suback_write(ctx, client, &sub_result);
            break;
        }
        case UNSUBSCRIBE:
            // TODO
            break;
        case PUBLISH: {
            Published_Message *out = find_free_published_message(ctx);
            if (out) {
                result = mqtt_publish_read(ctx, client, out);
                if (result == MQTT_DECODE_SUCCESS)
                    mqtt_publish_fanout_write(ctx, client, out);
            }
            break;
        }
        case PUBACK: {
            int16 mid = 0;
            mqtt_ack_read(ctx, client, &mid);
            update_message_delivery(ctx, client->conn_id, mid, MSG_ACKNOWLEDGED);
            break;
        }
        case PUBREC: {
            int16 mid = 0;
            result    = mqtt_ack_read(ctx, client, &mid);
            if (result == MQTT_DECODE_SUCCESS) {
                mqtt_ack_write(ctx, client, PUBREL, mid);
                update_message_delivery(ctx, client->conn_id, mid, MSG_AWAITING_PUBCOMP);
            }
            break;
        }
        case PUBREL: {
            int16 mid = 0;
            result    = mqtt_ack_read(ctx, client, &mid);
            if (result == MQTT_DECODE_SUCCESS) {
                mqtt_ack_write(ctx, client, PUBCOMP, mid);
                update_message_delivery(ctx, client->conn_id, mid, MSG_ACKNOWLEDGED);
            }
            break;
        }
        case PUBCOMP: {
            int16 mid = 0;
            mqtt_ack_read(ctx, client, &mid);
            update_message_delivery(ctx, client->conn_id, mid, MSG_ACKNOWLEDGED);
            break;
        }
        case PINGREQ:
            result = mqtt_pingreq_read(ctx, client);
            if (result == MQTT_DECODE_SUCCESS)
                mqtt_pingresp_write(ctx, client);
            break;
        default:
            log_error(">>>>: Unknown packet received %d (%ld)", mqtt_type_get(header), nread);
            buffer_skip(buf, buffer_available(buf));
            break;
        }

        broadcast_replies(ctx);
    }

    buffer_reset(&cdata->send_buffer);

    return TRANSPORT_SUCCESS;
}

static void client_connection_add(Tera_Context *ctx, int fd)
{
    // Already registered
    if (ctx->connection_data[fd].socket_fd > 0)
        return;

    // TODO relying on file descriptor uniqueness is poor logic
    //      think of a better approach
    ctx->connection_data[fd].socket_fd = fd;
    ctx->client_data[fd].conn_id       = fd;

    void *read_buf                     = arena_alloc(&io_arena, MAX_PACKET_SIZE);
    if (!read_buf)
        log_critical(">>>>: bump arena OOM");
    buffer_init(&ctx->connection_data[fd].recv_buffer, read_buf, MAX_PACKET_SIZE);

    void *write_buf = arena_alloc(&io_arena, MAX_PACKET_SIZE);
    if (!write_buf)
        log_critical(">>>>: bump arena OOM");
    buffer_init(&ctx->connection_data[fd].send_buffer, write_buf, MAX_PACKET_SIZE);
}

static void client_connection_shutdown(Tera_Context *ctx, int fd)
{
    for (usize i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
        if (ctx->subscription_data[i].client_id == fd)
            ctx->subscription_data[i].active = false;
    }
    ctx->connection_data[fd].socket_fd = -1;
    ctx->connection_data[fd].connected = false;
    close(fd);
    log_info(">>>>: Client disconnected");
}

static int server_start(Tera_Context *ctx, int serverfd)
{
    int numevents          = 0;
    Transport_Result err   = 0;
    time_t current_time    = 0;
    time_t check_delta     = 0;
    time_t last_check      = 0;
    time_t resend_check_ms = MQTT_RETRANSMISSION_CHECK_MS;

    iomux_t *iomux         = iomux_create();
    if (!iomux)
        return -1;

    iomux_add(iomux, serverfd, IOMUX_READ);

    while (1) {
        numevents = iomux_wait(iomux, resend_check_ms);
        if (numevents < 0)
            log_critical(">>>>: iomux error: %s", strerror(errno));

        for (int i = 0; i < numevents; ++i) {
            int fd = iomux_get_event_fd(iomux, i);

            if (fd == serverfd) {
                // New connection
                int clientfd = net_tcp_accept(serverfd, 1);
                if (clientfd < 0) {
                    log_error(">>>>: accept() error: %s", strerror(errno));
                    continue;
                }

                if (ctx->connection_data[clientfd].socket_fd == clientfd) {
                    log_warning(">>>>: Client connecting on an open socket");
                    continue;
                }

                log_info(">>>>: New client connected");
                iomux_add(iomux, clientfd, IOMUX_READ);
                client_connection_add(ctx, clientfd);

                err = process_client_messages(ctx, clientfd);
                if (err == TRANSPORT_DISCONNECT) {
                    client_connection_shutdown(ctx, fd);
                    continue;
                }

            } else if (ctx->connection_data[fd].socket_fd == fd) {
                err = process_client_messages(ctx, fd);
                if (err == TRANSPORT_DISCONNECT) {
                    client_connection_shutdown(ctx, fd);
                    continue;
                }
            }
        }

        // Periodic check for deliveries, some clients may fail to acknowledge
        // the PUBLISH messages, the reason can be anything, network faults
        // among the most common. This check ensure that a number of attempts
        // is retried before finally giving up.
        current_time = current_millis();
        check_delta  = current_time - last_check;
        if (check_delta >= resend_check_ms) {
            process_delivery_timeouts(ctx, current_time);
            last_check      = current_time;
            resend_check_ms = MQTT_RETRANSMISSION_CHECK_MS;
        } else {
            resend_check_ms = MQTT_RETRANSMISSION_CHECK_MS - check_delta;
        }
    }

    iomux_free(iomux);
    close(serverfd);

    return 0;
}

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 16768

int main(void)
{
    tera_context_init(&context);
    int serverfd = net_tcp_listen(DEFAULT_HOST, DEFAULT_PORT, 1);
    if (serverfd < 0)
        return -1;
    server_start(&context, serverfd);
    return 0;
}
