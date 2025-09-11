#include "buffer.h"
#include "bin.h"
#include "net.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

void buffer_init(Buffer *buffer, void *back_buffer, usize size)
{
    buffer->data = back_buffer;
    buffer->size = size;
    buffer_reset(buffer);
}

void buffer_reset(Buffer *buffer)
{
    buffer->read_pos  = 0;
    buffer->write_pos = 0;
}

int buffer_write(Buffer *buffer, const void *data, usize length)
{
    if (!buffer || !data)
        return -1;

    if (length + buffer->write_pos > buffer->size)
        return -1;

    memcpy(buffer->data + buffer->write_pos, data, length);
    buffer->write_pos += length;

    return 0;
}

int buffer_read(Buffer *buffer, void *out, usize length)
{
    if (!buffer || !out)
        return -1;

    if (buffer->read_pos + length > buffer->size)
        return -1;

    memcpy(out, buffer->data + buffer->read_pos, length);
    buffer->read_pos += length;

    return 0;
}

int buffer_peek(Buffer *buffer, void *out, usize length)
{
    if (!buffer || !out)
        return -1;

    if (buffer->read_pos + length > buffer->size)
        return -1;

    memcpy(out, buffer->data + buffer->read_pos, length);

    return 0;
}

int buffer_skip(Buffer *buffer, usize length)
{
    if (!buffer)
        return -1;

    if (buffer->read_pos + length > buffer->size)
        return -1;

    buffer->read_pos += length;
    return length;
}

bool buffer_is_empty(const Buffer *buffer) { return buffer->read_pos >= buffer->write_pos; }
usize buffer_available(const Buffer *buffer) { return buffer->write_pos - buffer->read_pos; }

isize buffer_net_recv(Buffer *buffer, int fd)
{

    if (!buffer || fd < 0)
        return -1;

    isize bytes_read = net_recv_nonblocking(fd, buffer->data + buffer->write_pos, buffer->size);
    if (bytes_read < 0) {
        // No data available right now
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /*
             * We have an EAGAIN error, which is really just signaling that
             * for some reasons the kernel is not ready to read more bytes at
             * the moment and it would block, so we just want to re-try some
             * time later, re-enqueuing a new read event
             */
            return -1;
        }
        /*
         * We got an unexpected error or a disconnection from the
         * client side, remove client from the global map and
         * free resources allocated such as io_event structure and
         * paired payload
         */

        // TODO Handle client disconnection here
    }

    if (bytes_read == 0)
        return 1; // EOF, but not an error

    buffer->write_pos += bytes_read;

    return bytes_read;
}

isize buffer_net_send(Buffer *buffer, int fd)
{
    if (!buffer || fd < 0)
        return -1;

    size_t available = buffer->size - buffer->read_pos;
    if (available == 0)
        return 0;

    isize bytes_written =
        net_send_nonblocking(fd, buffer->data + buffer->read_pos, buffer->write_pos);

    if (bytes_written < 0)
        return -1;

    buffer->read_pos += bytes_written;

    return bytes_written;
}

/*
 * buffer_read_struct() -- unpack data dictated by the format string into the
 * buffer
 *
 *   bits |signed   unsigned   float   string
 *   -----+----------------------------------
 *      8 |   b        B
 *     16 |   h        H         f
 *     32 |   i        I         d
 *     64 |   q        Q         g
 *      - |                               s
 *
 *  (string is extracted based on its stored length, but 's' can be
 *  prepended with a max length)
 */
usize buffer_read_struct(Buffer *buffer, const char *fmt, ...)
{
    va_list ap;

    int8 *b; // 8-bit
    uint8 *B;

    int16 *h; // 16-bit
    uint16 *H;

    int32 *i; // 32-bit
    uint32 *I;

    int64 *q; // 64-bit
    uint64 *Q;

    char *s; // strings
    usize maxstrlen = 0, size = buffer->read_pos;

    va_start(ap, fmt);

    for (; *fmt != '\0'; fmt++) {

        uint8 *buf = buffer->data + buffer->read_pos;

        switch (*fmt) {
        case 'b': // 8-bit
            b = va_arg(ap, int8 *);
            if (*buf <= 0x7f)
                *b = *buf; // re-sign
            else
                *b = -1 - (uint8)(0xffu - *buf);
            buffer->read_pos++;
            break;

        case 'B': // 8-bit unsigned
            B  = va_arg(ap, uint8 *);
            *B = *buf;
            buffer->read_pos++;
            break;

        case 'h': // 16-bit
            h  = va_arg(ap, int16 *);
            *h = bin_read_i16(buf);
            buffer->read_pos += 2;
            break;

        case 'H': // 16-bit unsigned
            H  = va_arg(ap, uint16 *);
            *H = bin_read_u16(buf);
            buffer->read_pos += 2;
            break;

        case 'i': // 32-bit
            i  = va_arg(ap, int32 *);
            *i = bin_read_i32(buf);
            buffer->read_pos += 4;
            break;

        case 'I': // 32-bit unsigned
            I  = va_arg(ap, uint32 *);
            *I = bin_read_u32(buf);
            buffer->read_pos += 4;
            break;

        case 'q': // 64-bit
            q  = va_arg(ap, int64 *);
            *q = bin_read_i64(buf);
            buffer->read_pos += 8;
            break;

        case 'Q': // 64-bit unsigned
            Q  = va_arg(ap, uint64 *);
            *Q = bin_read_u64(buf);
            buffer->read_pos += 8;
            break;

        case 's': // string
            s = va_arg(ap, char *);
            memcpy(s, buf, maxstrlen);
            s[maxstrlen] = '\0';
            buffer->read_pos += maxstrlen;
            break;

        default:
            if (isdigit(*fmt)) // track max str len
                maxstrlen = maxstrlen * 10 + (*fmt - '0');
        }

        if (!isdigit(*fmt))
            maxstrlen = 0;
    }

    va_end(ap);

    size = buffer->read_pos - size;

    return size;
}

/*
 * buffer_write_struct() -- store data dictated by the format string in the
 * buffer
 *
 *   bits |signed   unsigned   float   string
 *   -----+----------------------------------
 *      8 |   b        B
 *     16 |   h        H         f
 *     32 |   i        I         d
 *     64 |   q        Q         g
 *      - |                               s
 *
 *  (16-bit unsigned length is automatically prepended to strings)
 */
usize buffer_write_struct(Buffer *buffer, const char *fmt, ...)
{
    va_list ap;

    int8 b; // 8-bit
    uint8 B;

    int16 h; // 16-bit
    uint16 H;

    int32 i; // 32-bit
    uint32 I;

    int64 q; // 64-bit
    uint64 Q;

    char *s; // strings
    usize maxstrlen = 0, size = buffer->write_pos;

    va_start(ap, fmt);

    for (; *fmt != '\0'; fmt++) {
        uint8 *buf = buffer->data + buffer->write_pos;
        switch (*fmt) {
        case 'b':                           // 8-bit
            b    = (int8)va_arg(ap, int32); // promoted
            *buf = b;
            buffer->write_pos++;
            break;

        case 'B':                             // 8-bit unsigned
            B    = (uint8)va_arg(ap, uint32); // promoted
            *buf = B;
            buffer->write_pos++;
            break;

        case 'h': // 16-bit
            h = va_arg(ap, int32);
            bin_write_i16(buf, h);
            buffer->write_pos += 2;
            break;

        case 'H': // 16-bit unsigned
            H = va_arg(ap, uint32);
            bin_write_u16(buf, H);
            buffer->write_pos += 2;
            break;

        case 'i': // 32-bit
            i = va_arg(ap, int32);
            bin_write_i32(buf, i);
            buffer->write_pos += 4;
            break;

        case 'I': // 32-bit unsigned
            I = va_arg(ap, uint32);
            bin_write_i32(buf, I);
            buffer->write_pos += 4;
            break;

        case 'q': // 64-bit
            q = va_arg(ap, int64);
            bin_write_i64(buf, q);
            buffer->write_pos += 8;
            break;

        case 'Q': // 64-bit unsigned
            Q = va_arg(ap, uint64);
            bin_write_i64(buf, Q);
            buffer->write_pos += 8;
            break;

        case 's': // string
            s = va_arg(ap, char *);
            memcpy(buf, s, maxstrlen);
            s[maxstrlen] = '\0';
            buffer->write_pos += maxstrlen;
            break;

        default:
            if (isdigit(*fmt)) // track max str len
                maxstrlen = maxstrlen * 10 + (*fmt - '0');
        }

        if (!isdigit(*fmt))
            maxstrlen = 0;
    }

    va_end(ap);

    size = buffer->write_pos - size;

    return size;
}

usize buffer_read_binary(void *dst, Buffer *buf, usize len)
{
    char fmt[8] = {0};
    snprintf(fmt, 8, "%lus", len);
    return buffer_read_struct(buf, fmt, dst);
}

usize buffer_write_binary(Buffer *buf, const void *src, usize len)
{
    char fmt[8] = {0};
    snprintf(fmt, 8, "%lus", len);
    return buffer_write_struct(buf, fmt, src);
}

usize buffer_write_utf8_string(Buffer *buf, const void *src, usize len)
{
    return buffer_write_struct(buf, "H", len) + buffer_write_binary(buf, src, len);
}
