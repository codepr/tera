#pragma once

#include "logger.h"
#include "types.h"
#include <assert.h>

/**
 ** Dynamic array utility macros
 **/

#define darray(T)                                                              \
    struct {                                                                   \
        usize length;                                                          \
        usize capacity;                                                        \
        T *items;                                                              \
    }

#define da_reset(da) (da)->length = 0

#define da_extend(da)                                                          \
    do {                                                                       \
        (da)->capacity += 1;                                                   \
        (da)->capacity *= 2;                                                   \
        (da)->items =                                                          \
            realloc((da)->items, (da)->capacity * sizeof(*(da)->items));       \
        if (!(da)->items) {                                                    \
            log_critical("DA realloc failed");                                 \
        }                                                                      \
    } while (0)

#define da_append(da, item)                                                    \
    do {                                                                       \
        assert((da));                                                          \
        if ((da)->length + 1 >= (da)->capacity)                                \
            da_extend((da));                                                   \
        if (!(da)->items)                                                      \
            (da)->items = calloc((da)->capacity, sizeof(*(da)->items));        \
        (da)->items[(da)->length++] = (item);                                  \
    } while (0)

#define da_insert(da, i, item)                                                 \
    do {                                                                       \
        assert((da));                                                          \
        if ((i) >= (da)->length)                                               \
            da_extend((da));                                                   \
        (da)->items[(i)] = (item);                                             \
        if ((i) >= (da)->length)                                               \
            (da)->length++;                                                    \
    } while (0)

#define da_insert_shift(da, i, item)                                           \
    do {                                                                       \
        assert((da));                                                          \
        if ((da)->length == 0) {                                               \
            da_append((da), (item));                                           \
        } else {                                                               \
            if ((da)->length + 1 == (da)->capacity)                            \
                da_extend((da));                                               \
            memmove((da)->items + (i) + 1, (da)->items + (i),                  \
                    ((da)->length - (i) + 1) * sizeof((da)->items[0]));        \
            (da)->items[(i)] = (item);                                         \
            (da)->length++;                                                    \
        }                                                                      \
    } while (0)

#define da_search(da, target, cmp, res)                                        \
    do {                                                                       \
        *(res) = -1;                                                           \
        if ((da)->length && (cmp)(&(da)->items[0], (target)) < 1) {            \
            for (usize i = 0; i < (da)->length; ++i) {                         \
                if ((cmp)(&(da)->items[i], (target)) == 0) {                   \
                    *(res) = i;                                                \
                }                                                              \
            }                                                                  \
        }                                                                      \
    } while (0)

#define da_bsearch(da, target, cmp, res)                                       \
    do {                                                                       \
        if ((da)->length > 0) {                                                \
            if ((cmp)(&(da)->items[0], (target)) >= 0) {                       \
                *(res) = 0;                                                    \
            } else if ((cmp)(&(da)->items[(da)->length - 1], (target)) <= 0) { \
                *(res) = (da)->length - 1;                                     \
            } else {                                                           \
                usize left = 0, middle = 0, right = (da)->length - 1;          \
                int found = 0;                                                 \
                while (left <= right) {                                        \
                    middle = floor((left + right) / 2);                        \
                    if ((cmp)(&(da)->items[middle], (target)) < 0) {           \
                        left = middle + 1;                                     \
                    } else if ((cmp)(&(da)->items[middle], (target)) > 0) {    \
                        right = middle - 1;                                    \
                    } else {                                                   \
                        *(res) = middle;                                       \
                        found  = 1;                                            \
                        break;                                                 \
                    }                                                          \
                }                                                              \
                if (found == 0) {                                              \
                    *(res) = left;                                             \
                }                                                              \
            }                                                                  \
        } else {                                                               \
            *(res) = 0;                                                        \
        }                                                                      \
    } while (0)

#define da_back(da)       (da)->items[(da)->length == 0 ? 0 : (da)->length - 1]

#define da_get(da, index) (da).items[(index)]

#define da_free(da)                                                            \
    do {                                                                       \
        if ((da)->items) {                                                     \
            free((da)->items);                                                 \
            (da)->items = NULL;                                                \
        }                                                                      \
    } while (0)
