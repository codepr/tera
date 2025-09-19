#pragma once

#include "types.h"

struct arena {
    uint8 *buf;
    uint32 size;
    uint32 prev_offset;
    uint32 curr_offset;
};

typedef struct arena Arena;

void arena_init(Arena *a, void *buffer, uint32 buffer_size);
void *arena_alloc(Arena *a, uint32 size);
void *arena_at(const Arena *a, uintptr_t offset);
void arena_reset(Arena *a);
uintptr_t arena_current_offset(const Arena *a);
void arena_dump(const Arena *a);
