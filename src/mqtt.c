#include "mqtt.h"

static int mqtt_read_variable_byte_integer(Buffer *buf, uint32 *value)
{
    uint8_t byte;
    uint32_t result     = 0;
    uint32_t multiplier = 1;
    int bytes_read      = 0;

    *value              = 0; // Initialize output

    do {
        // Check buffer bounds
        if (buf->read_pos >= buf->size) {
            return -1; // Buffer underrun
        }

        // Check MQTT specification limit (max 4 bytes)
        if (bytes_read >= 4) {
            return -1; // Malformed Variable Byte Integer
        }

        byte = buf->data[buf->read_pos];
        buf->read_pos++;
        bytes_read++;

        result += (byte & 0x7F) * multiplier;

        // Check for overflow before next iteration
        if (multiplier > (UINT32_MAX / 128)) {
            return -1; // Would overflow on next iteration
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
usize mqtt_variable_length_read(Buffer *buf, usize *len)
{
    uint32_t value;
    int result = mqtt_read_variable_byte_integer(buf, &value);

    if (result < 0) {
        *len = 0;
        return 0; // Error - could also return -1 to indicate error
    }

    *len = (usize)value; // Safe since we validated range
    return (usize)result;
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
usize mqtt_variable_length_write(Buffer *buf, usize len)
{
    usize bytes    = 0;
    uint16 encoded = 0;

    do {
        if (buf->write_pos + 1 > MAX_VARIABLE_LENGTH_BYTES)
            return bytes;

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
