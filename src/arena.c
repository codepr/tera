#include "arena.h"
#include <string.h>

#define DEFAULT_ALIGNMENT  (2 * sizeof(uint16))
#define is_power_of_two(x) ((x != 0) && ((x & (x - 1)) == 0))

void arena_init(Arena *a, void *buffer, usize buffer_size)
{
    a->buf         = buffer;
    a->size        = buffer_size;
    a->prev_offset = 0;
    a->curr_offset = 0;
}

static uintptr_t align_forward(uintptr_t ptr, usize align)
{
    uintptr_t p      = ptr;
    uintptr_t a      = (uintptr_t)align;
    uintptr_t modulo = p & (a - 1);

    if (modulo != 0) {
        p += a - modulo;
    }

    return p;
}

static void *arena_alloc_align(Arena *a, usize size, usize align)
{
    uintptr_t curr_ptr = (uintptr_t)a->buf + (uintptr_t)a->curr_offset;
    uintptr_t offset   = align_forward(curr_ptr, align);
    offset -= (uintptr_t)a->buf;

    if (offset + size <= a->size) {
        void *ptr      = &a->buf[offset];
        a->prev_offset = offset;
        a->curr_offset = offset + size;

        // Zero new memory by default
        // TODO remove if back buffer alloc'ed with calloc
        memset(ptr, 0, size);
        return ptr;
    }

    return NULL;
}

uintptr_t arena_current_offset(const Arena *a)
{
    uintptr_t curr_ptr = (uintptr_t)a->buf + (uintptr_t)a->curr_offset;
    uintptr_t offset   = align_forward(curr_ptr, DEFAULT_ALIGNMENT);
    offset -= (uintptr_t)a->buf;

    return offset;
}

void *arena_alloc(Arena *a, usize size) { return arena_alloc_align(a, size, DEFAULT_ALIGNMENT); }

void arena_reset(Arena *a)
{
    a->curr_offset = 0;
    a->prev_offset = 0;
}

void arena_dump(const Arena *a)
{
    for (int i = 0; i < a->curr_offset; ++i) {
        printf("%02x ", a->buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    printf("\n");
}
