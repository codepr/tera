#pragma once

#include "types.h"
#include <stdbool.h>

typedef struct buffer {
    uint8 *data;
    uint32 size;
    uint32 read_pos;
    uint32 write_pos;
} Buffer;

void buffer_init(Buffer *buffer, void *back_buffer, uint32 size);
void buffer_reset(Buffer *buffer);

int buffer_write(Buffer *buffer, const void *data, uint32 length);
int buffer_read(Buffer *buffer, void *out, uint32 length);
int buffer_peek(Buffer *buffer, void *out, uint32 length);
int buffer_skip(Buffer *buffer, uint32 length);
uint32 buffer_available(const Buffer *buffer);
bool buffer_is_empty(const Buffer *buffer);

// Network utilities
isize buffer_net_recv(Buffer *buffer, int fd);
isize buffer_net_send(Buffer *buffer, int fd);

// Encoding utilities
uint32 buffer_read_struct(Buffer *buffer, const char *fmt, ...);
uint32 buffer_write_struct(Buffer *buffer, const char *fmt, ...);
uint32 buffer_read_binary(void *dst, Buffer *buf, uint32 len);
uint32 buffer_write_binary(Buffer *buf, const void *src, uint32 len);
uint32 buffer_write_utf8_string(Buffer *buf, const void *src, uint32 len);

void buffer_dump(const Buffer *buf);
