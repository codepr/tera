#pragma once

#include "types.h"

struct arena {
    uint8 *buf;
    usize size;
    usize prev_offset;
    usize curr_offset;
};

typedef struct arena Arena;

void arena_init(Arena *a, void *buffer, usize buffer_size);
void *arena_alloc(Arena *a, usize size);
void *arena_at(const Arena *a, uintptr_t offset);
void arena_reset(Arena *a);
uintptr_t arena_current_offset(const Arena *a);
void arena_dump(const Arena *a);
