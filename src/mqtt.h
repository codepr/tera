#pragma once

#include "arena.h"
#include "buffer.h"
#include "logger.h"
#include "types.h"

#define MAX_VARIABLE_LENGTH_BYTES 4

/* Message types */
typedef enum packet_type {
    CONNECT     = 1,
    CONNACK     = 2,
    PUBLISH     = 3,
    PUBACK      = 4,
    PUBREC      = 5,
    PUBREL      = 6,
    PUBCOMP     = 7,
    SUBSCRIBE   = 8,
    SUBACK      = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK    = 11,
    PINGREQ     = 12,
    PINGRESP    = 13,
    DISCONNECT  = 14
} Packet_Type;

typedef enum qos_level { AT_MOST_ONCE, AT_LEAST_ONCE, EXACTLY_ONCE } QoS_Level;

// =============================================================================
// CONNECT PACKET VARIABLE HEADER - Connect Flags
// =============================================================================

/*
 * CONNECT Variable Header Connect Flags Layout (byte 8 in CONNECT packet):
 * Bit:     7        6         5          4    3       2         1           0
 * Field:   username password will_retain will_qos(1) will_qos(0) will clean_start reserved
 */

static inline uint8 mqtt_clean_session_get(uint8 byte) { return (((byte) >> 1) & 0x01); }
static inline uint8 mqtt_will_get(uint8 byte) { return (((byte) >> 2) & 0x01); }
static inline uint8 mqtt_will_qos_get(uint8 byte) { return (((byte) >> 3) & 0x03); }
static inline uint8 mqtt_will_retain_get(uint8 byte) { return (((byte) >> 5) & 0x01); }
static inline uint8 mqtt_password_get(uint8 byte) { return (((byte) >> 6) & 0x01); }
static inline uint8 mqtt_username_get(uint8 byte) { return (((byte) >> 7) & 0x01); }

// Connect header set MQTT bitfield flags

static inline uint8 mqtt_clean_session_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0x02) | (((value) & 0x01) << 1);
}

static inline uint8 mqtt_will_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0x04) | (((value) & 0x01) << 2);
}

static inline uint8 mqtt_will_qos_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0x18) | (((value) & 0x03) << 3);
}

static inline uint8 mqtt_will_retain_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0x20) | (((value) & 0x01) << 5);
}

static inline uint8 mqtt_password_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0x40) | (((value) & 0x01) << 6);
}

static inline uint8 mqtt_username_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0x80) | (((value) & 0x01) << 7);
}

// =============================================================================
// PUBLISH PACKET SPECIFIC FLAGS (when packet type = 3)
// =============================================================================

/*
 * PUBLISH Fixed Header Flags Layout:
 * Bit:     3    2       1       0
 * Field:   DUP QoS(1) QoS(0) RETAIN
 */
static inline uint8 mqtt_retain_get(uint8 byte) { return (((byte) >> 0) & 0x01); }
static inline uint8 mqtt_qos_get(uint8 byte) { return (((byte) >> 1) & 0x03); }
static inline uint8 mqtt_dup_get(uint8 byte) { return (((byte) >> 3) & 0x01); }
static inline uint8 mqtt_type_get(uint8 byte) { return (((byte) >> 4) & 0x0F); }

// Variable header set MQTT bitfield flags

static inline uint8 mqtt_retain_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0x01) | ((value) & 0x01);
}

static inline uint8 mqtt_qos_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0x06) | (((value) & 0x03) << 1);
}

static inline uint8 mqtt_dup_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0x08) | (((value) & 0x01) << 3);
}

static inline uint8 mqtt_type_set(uint8 byte, uint8 value)
{
    return ((byte) & ~0xF0) | (((value) & 0x0F) << 4);
}

typedef struct subscription_data {
    uint16 client_id; // Index of the subscribing client in Client_Data array
    uint16 topic_offset;
    uint16 topic_size;
    uint16 mid;
    int16 id;
    uint8 options;
    bool active;
} Subscription_Data;

// Message_Delivery flags for retransmission states
typedef enum delivery_state {
    MSG_PENDING_SEND     = 0, // Ready to send
    MSG_AWAITING_PUBACK  = 1, // QoS1: waiting for PUBACK
    MSG_AWAITING_PUBREC  = 2, // QoS2: waiting for PUBREC
    MSG_AWAITING_PUBREL  = 3, // QoS2: waiting for PUBREC
    MSG_AWAITING_PUBCOMP = 4, // QoS2: sent PUBREL, waiting for PUBREL
    MSG_ACKNOWLEDGED     = 5, // Fully acknowledged
    MSG_EXPIRED          = 6, // Retry limit exceeded
} Delivery_State;

typedef struct message_delivery {
    // Retransmission fields
    int64 last_sent_at;   // Last transmission timestamp
    int64 next_retry_at;  // When to retry (0 = don't retry)
    uint16 retry_count;   // Number of retries attempted
    Delivery_State state; // Current delivery state

    // Message metadata for topic, payload
    uint16 client_id;        // Target client (subscriber)
    uint16 published_msg_id; // Points to a Published_Message
    uint16 message_id;       // MQTT packet ID for client
    uint8 delivery_qos;      // Negotiated QoS (min between publisher/subscriber)
    bool active;             // Wether the delivery is free to be used
} Message_Delivery;

typedef struct published_message {
    // Message metadata for topic, payload
    uint16 id;
    uint16 property_id;
    uint16 topic_size;
    uint16 topic_offset;
    uint16 message_size;
    uint16 message_offset;
    uint8 options;
} Published_Message;

typedef struct client_data Client_Data;
typedef struct tera_context Tera_Context;

typedef enum {
    MQTT_DECODE_SUCCESS    = 0,
    MQTT_DECODE_ERROR      = -1,
    MQTT_DECODE_INCOMPLETE = -2,
    MQTT_DECODE_INVALID    = -3
} MQTT_Decode_Result;

static inline usize mqtt_variable_length_encoded_length(usize value)
{
    if (value < 128)
        return 1;
    if (value < 16384)
        return 2;
    if (value < 2097152)
        return 3;
    return 4;
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
usize mqtt_variable_length_read(Buffer *buf, usize *len);

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
usize mqtt_variable_length_write(Buffer *buf, usize len);

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
typedef struct fixed_header {
    union {
        uint8 byte;
        struct {
            uint8 retain : 1;
            uint8 qos : 2;
            uint8 dup : 1;
            uint8 type : 4;
        } bits;
    };
    usize remaining_length;
} Fixed_Header;

// TODO consider returning the amount of read bytes
static inline isize mqtt_fixed_header_read(Buffer *buf, Fixed_Header *header)
{
    isize read_bytes = 0;
    usize start_pos  = buf->read_pos;

    // Read packet type and flags
    if (buffer_read_struct(buf, "B", &header->byte) != sizeof(uint8)) {
        log_error("Failed to read packet header");
        return MQTT_DECODE_ERROR;
    }
    read_bytes       = sizeof(uint8);

    // Read remaining length
    int length_bytes = mqtt_variable_length_read(buf, &header->remaining_length);
    if (length_bytes < 0) {
        log_error("Invalid variable length encoding");
        buf->read_pos = start_pos; // Restore position
        return MQTT_DECODE_INCOMPLETE;
    }
    read_bytes += length_bytes;

    // Validate we have enough data for the complete packet
    usize total_packet_size =
        sizeof(uint8) + length_bytes + header->remaining_length; // header + length + payload
    if (start_pos + total_packet_size > buf->size) {
        log_debug("Incomplete packet - need %zu more bytes",
                  (start_pos + total_packet_size) - buf->size);
        buf->read_pos = start_pos; // Restore position
        return MQTT_DECODE_INCOMPLETE;
    }

    return read_bytes;
}

static inline isize mqtt_fixed_header_write(Buffer *buf, Fixed_Header *header)
{
    isize written_bytes = 0;

    if (buffer_write_struct(buf, "B", header->byte) != sizeof(uint8)) {
        log_error("Failed to read packet header");
        return MQTT_DECODE_ERROR;
    }
    written_bytes += sizeof(uint8);

    written_bytes += mqtt_variable_length_write(buf, header->remaining_length);

    return written_bytes;
}

/*
 * MQTT Connect packet unpack function, it's the first mandatory packet that
 * a new client must send after the socket connection went ok. As described in
 * the MQTT v3.1.1 specs the packet has the following form:
 *
 * |   Bit    |  7  |  6  |  5  |  4  |  3  |  2  |  1  |   0    |
 * |----------|-----------------------|--------------------------| Fixed Header
 * | Byte 1   |      MQTT type 3      | dup |    QoS    | retain |
 * |----------|--------------------------------------------------|
 * | Byte 2   |                                                  |
 * |   .      |               Remaining Length                   |
 * |   .      |                                                  |
 * | Byte 5   |                                                  |
 * |----------|--------------------------------------------------| Variable
 * | Byte 6   |             Protocol name len MSB                |   Header
 * | Byte 7   |             Protocol name len LSB                | [UINT16]
 * |----------|--------------------------------------------------|
 * | Byte 8   |                                                  |
 * |   .      |                'M' 'Q' 'T' 'T'                   |
 * | Byte 12  |                                                  |
 * |----------|--------------------------------------------------|
 * | Byte 13  |                 Protocol level                   |
 * |----------|--------------------------------------------------|
 * |          |                 Connect flags                    |
 * | Byte 14  |--------------------------------------------------|
 * |          |  U  |  P  |  WR |     WQ    |  WF |  CS |    R   |
 * |----------|--------------------------------------------------|
 * | Byte 15  |                 Keepalive MSB                    | [UINT16]
 * | Byte 17  |                 Keepalive LSB                    |
 * |----------|--------------------------------------------------| Payload
 * | Byte 18  |             Client ID length MSB                 |
 * | Byte 19  |             Client ID length LSB                 | [UINT16]
 * |----------|--------------------------------------------------|
 * | Byte 20  |                                                  |
 * |   .      |                  Client ID                       |
 * | Byte N   |                                                  |
 * |----------|--------------------------------------------------|
 * | Byte N+1 |              Username length MSB                 |
 * | Byte N+2 |              Username length LSB                 |
 * |----------|--------------------------------------------------|
 * | Byte N+3 |                                                  |
 * |   .      |                  Username                        |
 * | Byte M   |                                                  |
 * |----------|--------------------------------------------------|
 * | Byte M+1 |              Password length MSB                 |
 * | Byte M+2 |              Password length LSB                 |
 * |----------|--------------------------------------------------|
 * | Byte M+3 |                                                  |
 * |   .      |                  Password                        |
 * | Byte M+K |                                                  |
 *
 * All that the function do is just read bytes sequentially according to their
 * storing type, strings are treated as arrays of unsigned char.
 * The example assume that the remaining length uses all the dedicated bytes
 * but in reality it can be even just one byte length; the function starts to
 * unpack from the Variable Header position to the end of the packet as stated
 * by the total length expected.
 */
MQTT_Decode_Result mqtt_connect_read(Tera_Context *ctx, Client_Data *cdata);

MQTT_Decode_Result mqtt_disconnect_read(Tera_Context *ctx, const Client_Data *cdata);

// Debugging utilities
void mqtt_message_dump(const Buffer *buf, bool read);

#define MAX_SUBSCRIPTION_IDS 10

/*
 * MQTT v5.0 PUBLISH packet structure
 * Fixed Header:
 * - Byte 1: Packet Type (3) + DUP + QoS + RETAIN flags
 * - Remaining Length (Variable Byte Integer)
 *
 * Variable Header:
 * - Topic Name (UTF-8 String)
 * - Packet Identifier (if QoS > 0)
 * - Properties Length (Variable Byte Integer)
 * - Properties:
 *   - Payload Format Indicator (0x01)
 *   - Message Expiry Interval (0x02)
 *   - Content Type (0x03)
 *   - Response Topic (0x08)
 *   - Correlation Data (0x09)
 *   - Subscription Identifier (0x0B) - Can appear multiple times!
 *   - Topic Alias (0x23)
 *   - User Property (0x26) - Can appear multiple times
 *
 * Payload:
 * - Application Message (optional)
 */

// Property identifiers for PUBLISH
typedef enum {
    PUBLISH_PROP_PAYLOAD_FORMAT_INDICATOR = 0x01,
    PUBLISH_PROP_MESSAGE_EXPIRY_INTERVAL  = 0x02,
    PUBLISH_PROP_CONTENT_TYPE             = 0x03,
    PUBLISH_PROP_RESPONSE_TOPIC           = 0x08,
    PUBLISH_PROP_CORRELATION_DATA         = 0x09,
    PUBLISH_PROP_SUBSCRIPTION_IDENTIFIER  = 0x0B,
    PUBLISH_PROP_TOPIC_ALIAS              = 0x23,
    PUBLISH_PROP_USER_PROPERTY            = 0x26
} Publish_Property_Id;

// PUBLISH Properties structure
typedef struct publish_properties {
    bool active;

    // Basic properties
    bool has_payload_format;
    uint8 payload_format_indicator;

    bool has_message_expiry;
    uint32 message_expiry_interval;

    bool has_content_type;
    uint8 *content_type;
    uint16 content_type_len;

    bool has_response_topic;
    uint8 *response_topic;
    uint16 response_topic_len;

    bool has_correlation_data;
    uint8 *correlation_data;
    uint16 correlation_data_len;

    bool has_topic_alias;
    uint16 topic_alias;

    // Subscription Identifiers (multiple allowed!)
    uint32 subscription_ids[MAX_SUBSCRIPTION_IDS];
    uint8 subscription_id_count;

    // TODO User Properties (multiple allowed)
} Publish_Properties;

/*
 * MQTT Publish packet unpack function, as described in the MQTT v3.1.1 specs
 * the packet has the following form:
 *
 * |   Bit    |  7  |  6  |  5  |  4  |  3  |  2  |  1  |   0    |
 * |----------|-----------------------|--------------------------|<-- Fixed Header
 * | Byte 1   |      MQTT type 3      | dup |    QoS    | retain |
 * |----------|--------------------------------------------------|
 * | Byte 2   |                                                  |
 * |   .      |               Remaining Length                   |
 * |   .      |                                                  |
 * | Byte 5   |                                                  |
 * |----------|--------------------------------------------------|<-- Variable Header
 * | Byte 6   |                Topic len MSB                     |
 * | Byte 7   |                Topic len LSB                     |  [UINT16]
 * |----------|--------------------------------------------------|
 * | Byte 8   |                                                  |
 * |   .      |                Topic name                        |
 * | Byte N   |                                                  |
 * |----------|--------------------------------------------------|
 * | Byte N+1 |            Packet Identifier MSB                 |  [UINT16]
 * | Byte N+2 |            Packet Identifier LSB                 |
 * |----------|--------------------------------------------------|<-- Payload
 * | Byte N+3 |                                                  |
 * |   .      |                   Payload                        |
 * | Byte N+M |                                                  |
 *
 * All that the function do is just read bytes sequentially according to their
 * storing type, strings are treated as arrays of unsigned char.
 * The example assume that the remaining length uses all the dedicated bytes
 * but in reality it can be even just one byte length; the function starts to
 * unpack from the Variable Header position to the end of the packet as stated
 * by the total length expected.
 *
 * The functions accepts a pointer to a Published_Message, this is pointing to
 * a position in the published messages table, not in use by any previous message,
 * to be properly filled in with the metadata of the incoming PUBLISH.
 */
MQTT_Decode_Result mqtt_publish_read(Tera_Context *ctx, const Client_Data *cdata,
                                     Published_Message *message);

// Maximum topic filters per SUBSCRIBE packet (reasonable limit)
#define MAX_TOPIC_FILTERS_PER_SUBSCRIBE 50

// SUBSCRIBE result structure - tracks all subscriptions from one packet
typedef struct {
    uint16_t packet_id;
    uint8_t reason_codes[MAX_TOPIC_FILTERS_PER_SUBSCRIBE];
    uint8_t topic_filter_count;
    bool acknowledged;
} Subscribe_Result;

MQTT_Decode_Result mqtt_subscribe_read(Tera_Context *ctx, const Client_Data *cdata,
                                       Subscribe_Result *r);

MQTT_Decode_Result mqtt_unsubscribe_read(Tera_Context *ctx, const Client_Data *cdata, Arena *arena);

MQTT_Decode_Result mqtt_pingreq_read(Tera_Context *ctx, const Client_Data *cdata);

MQTT_Decode_Result mqtt_ack_read(Tera_Context *ctx, const Client_Data *cdata, int16 *mid);

typedef enum {
    CONNACK_SUCCESS                      = 0x00,
    CONNACK_UNSPECIFIED_ERROR            = 0x80,
    CONNACK_MALFORMED_PACKET             = 0x81,
    CONNACK_PROTOCOL_ERROR               = 0x82,
    CONNACK_UNSUPPORTED_PROTOCOL_VERSION = 0x84,
    CONNACK_CLIENT_ID_NOT_VALID          = 0x85,
    CONNACK_BAD_USERNAME_PASSWORD        = 0x86,
    CONNACK_NOT_AUTHORIZED               = 0x87,
    CONNACK_SERVER_UNAVAILABLE           = 0x88
} CONNACK_Reason_Code;

void mqtt_connack_write(Tera_Context *ctx, const Client_Data *cdata, CONNACK_Reason_Code rc);

typedef enum {
    SUBACK_SUCCESS_QOS_ZERO              = 0x00,
    SUBACK_SUCCESS_QOS_ONE               = 0x01,
    SUBACK_SUCCESS_QOS_TWO               = 0x02,
    SUBACK_UNSPECIFIED_ERROR             = 0x80,
    SUBACK_IMPLEMENTATION_SPECIFIC_ERROR = 0x83,
    SUBACK_NOT_AUTHORIZED                = 0x87
} SUBACK_Reason_Code;

void mqtt_suback_write(Tera_Context *ctx, const Client_Data *cdata, const Subscribe_Result *r);

/**
 * Write in serialized binary format the PUBLISH packet to be sent to all the
 * matching subscribers.
 * The functions accepts a pointer to a Published_Message, this is pointing to
 * a position in the published messages table, not in use by any previous message,
 * to be properly filled in with the metadata of the incoming PUBLISH.
 */
void mqtt_publish_fanout_write(Tera_Context *ctx, const Client_Data *cdata,
                               Published_Message *pub_msg);

/**
 * This function is meant to be used when a retry is attempted, so it assumes
 * a Message_Delivery is already active for a certain client, be it a publisher
 * or a subscriber.
 */
void mqtt_publish_retry(Tera_Context *ctx, Message_Delivery *delivery);

void mqtt_pingresp_write(Tera_Context *ctx, const Client_Data *cdata);

void mqtt_ack_write(Tera_Context *ctx, const Client_Data *cdata, Packet_Type ack_type, int16 id);

uint16 mqtt_subscription_next_mid(Subscription_Data *subscription_data);
