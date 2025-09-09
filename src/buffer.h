#pragma once

#include "types.h"
#include <stdbool.h>

typedef struct buffer {
    uint8 *data;
    usize size;
    usize read_pos;
    usize write_pos;
} Buffer;

void buffer_init(Buffer *buffer, void *back_buffer, usize size);
void buffer_reset(Buffer *buffer);

int buffer_write(Buffer *buffer, const void *data, usize length);
int buffer_read(Buffer *buffer, void *out, usize length);
int buffer_peek(Buffer *buffer, void *out, usize length);
int buffer_skip(Buffer *buffer, usize length);
usize buffer_available(const Buffer *buffer);
bool buffer_is_empty(const Buffer *buffer);

// Network utilities
isize buffer_net_recv(Buffer *buffer, int fd);
isize buffer_net_send(Buffer *buffer, int fd);

// Encoding utilities
usize buffer_read_struct(Buffer *buffer, const char *fmt, ...);
usize buffer_write_struct(Buffer *buffer, const char *fmt, ...);
usize buffer_read_binary(void *dst, Buffer *buf, usize len);
usize buffer_write_binary(Buffer *buf, const void *src, usize len);
