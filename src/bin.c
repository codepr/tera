#include "bin.h"

/**
 ** Binary protocol utility functions
 **
 ** A simple binary protocol to communicate over the wire. The RPC calls
 ** are pretty simple and easy to serialize.
 **/

int bin_write_u8(uint8 *buf, uint8 val)
{
    *buf++ = val;
    return sizeof(uint8);
}

uint8 bin_read_u8(const uint8 *const buf) { return ((uint8)*buf); }

// bin_write_u16() -- store a 16-bit int into a char buffer (like htonl())
int bin_write_u16(uint8 *buf, uint16 val)
{
    *buf++ = val >> 8;
    *buf++ = val;

    return sizeof(uint16);
}

// bin_write_i16() -- store a 16-bit int into a char buffer (like htonl())
int bin_write_i16(uint8 *buf, int16 val)
{
    *buf++ = val >> 8;
    *buf++ = val;

    return sizeof(int16);
}

// bin_write_u32() -- store a 32-bit int into a char buffer (like htonl())
int bin_write_u32(uint8 *buf, uint32 val)
{
    *buf++ = val >> 24;
    *buf++ = val >> 16;
    *buf++ = val >> 8;
    *buf++ = val;

    return sizeof(uint32);
}

/*
** bin_read_i16() -- unpack a 16-bit int from a char buffer (like ntohs())
*/
int16 bin_read_i16(const uint8 *const buf)
{
    uint16 i2 = ((uint16)buf[0] << 8) | buf[1];
    int16 val;

    // change unsigned numbers to signed
    if (i2 <= 0x7fffu)
        val = i2;
    else
        val = -1 - (uint16)(0xffffu - i2);

    return val;
}

// read_u16() -- unpack a 16-bit unsigned from a char buffer (like ntohl())
uint16 bin_read_u16(const uint8 *const buf) { return ((uint16)buf[0] << 8) | buf[1]; }

// read_u32() -- unpack a 32-bit unsigned from a char buffer (like ntohl())
uint32 bin_read_u32(const uint8 *const buf)
{
    return ((uint32)buf[0] << 24) | ((uint32)buf[1] << 16) | ((uint32)buf[2] << 8) | buf[3];
}

// bin_write_i32() -- store a 32-bit int into a char buffer (like htonl())
int bin_write_i32(uint8 *buf, int32 val)
{
    *buf++ = val >> 24;
    *buf++ = val >> 16;
    *buf++ = val >> 8;
    *buf++ = val;

    return sizeof(int32);
}

// bin_read_i32() -- unpack a 32-bit int from a char buffer (like ntohl())
int32 bin_read_i32(const uint8 *buf)
{
    uint32 i2 = ((int64)buf[0] << 24) | ((int64)buf[1] << 16) | ((int64)buf[2] << 8) | buf[3];
    int32 val;

    // change unsigned numbers to signed
    if (i2 <= 0x7fffffffu)
        val = i2;
    else
        val = -1 - (int64)(0xffffffffu - i2);

    return val;
}

/*
 * bin_write_i64() -- store a 64-bit int into a char buffer (like htonl())
 */
int bin_write_i64(uint8 *buf, int64 val)
{
    *buf++ = val >> 56;
    *buf++ = val >> 48;
    *buf++ = val >> 40;
    *buf++ = val >> 32;
    *buf++ = val >> 24;
    *buf++ = val >> 16;
    *buf++ = val >> 8;
    *buf++ = val;

    return sizeof(int64);
}

/*
 * bin_read_i64() -- unpack a 64-bit unsigned from a char buffer (like ntohl())
 */
int64 bin_read_i64(const uint8 *const buf)
{
    return ((uint64)buf[0] << 56) | ((uint64)buf[1] << 48) | ((uint64)buf[2] << 40) |
           ((uint64)buf[3] << 32) | ((uint64)buf[4] << 24) | ((uint64)buf[5] << 16) |
           ((uint64)buf[6] << 8) | buf[7];
}

/*
** bin_read_u64() -- unpack a 64-bit unsigned from a char buffer (like ntohl())
*/
uint64 bin_read_u64(const uint8 *const buf)
{
    return ((uint64)buf[0] << 56) | ((uint64)buf[1] << 48) | ((uint64)buf[2] << 40) |
           ((uint64)buf[3] << 32) | ((uint64)buf[4] << 24) | ((uint64)buf[5] << 16) |
           ((uint64)buf[6] << 8) | buf[7];
}

/*
 * write_f64() -- store a 64-bit float into a char buffer, taken from beej.us
 * guide
 */
int bin_write_f64(uint8 *buf, float64 val)
{
    unsigned bits = 64, expbits = 11;
    float64 fnorm;
    int shift;
    long long sign, exp, significand;
    unsigned significandbits = bits - expbits - 1; // -1 for sign bit

    if (val == 0.0) {
        bin_write_u32(buf, 0);
    } else {
        // check sign and begin normalization
        if (val < 0) {
            sign  = 1;
            fnorm = -val;
        } else {
            sign  = 0;
            fnorm = val;
        }

        // get the normalized form of f and track the exponent
        shift = 0;
        while (fnorm >= 2.0) {
            fnorm /= 2.0;
            shift++;
        }
        while (fnorm < 1.0) {
            fnorm *= 2.0;
            shift--;
        }
        fnorm       = fnorm - 1.0;

        // calculate the binary form (non-float) of the significand data
        significand = fnorm * ((1LL << significandbits) + 0.5f);

        // get the biased exponent
        exp         = shift + ((1 << (expbits - 1)) - 1); // shift + bias

        // return the final answer
        uint64 d    = (sign << (bits - 1)) | (exp << (bits - expbits - 1)) | significand;

        bin_write_i64(buf, d);
    }

    return sizeof(float64);
}

/*
 * read_f64() -- unpack a 64-bit float into a char buffer, taken from beej.us
 * guide
 */
float64 bin_read_f64(const uint8 *buf)
{
    uint64 i      = bin_read_i64(buf);
    unsigned bits = 64, expbits = 11;
    float64 result;
    int64 shift;
    unsigned bias;
    unsigned significandbits = bits - expbits - 1; // -1 for sign bit

    if (i == 0)
        return 0.0;

    // pull the significand
    result = (i & ((1LL << significandbits) - 1)); // mask
    result /= (1LL << significandbits);            // convert back to float
    result += 1.0f;                                // add the one back on

    // deal with the exponent
    bias  = (1 << (expbits - 1)) - 1;
    shift = ((i >> significandbits) & ((1LL << expbits) - 1)) - bias;
    while (shift > 0) {
        result *= 2.0;
        shift--;
    }
    while (shift < 0) {
        result /= 2.0;
        shift++;
    }

    // sign it
    result *= (i >> (bits - 1)) & 1 ? -1.0 : 1.0;

    return result;
}
