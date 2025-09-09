#pragma once

#include "timeutil.h"
#include <stdlib.h>

#define LL_DEBUG    0
#define LL_INFO     1
#define LL_WARNING  2
#define LL_ERROR    3
#define LL_CRITICAL 4

#define LOG_LEVEL   LL_DEBUG

#define LOG(level, level_str, fmt, ...)                                                            \
    do {                                                                                           \
        if (level >= LOG_LEVEL) {                                                                  \
            fprintf(stderr, "%lli %s " fmt "%s", current_micros(),                                 \
                    level_str __VA_OPT__(, ) __VA_ARGS__, "\n");                                   \
        }                                                                                          \
        if (level == LL_CRITICAL)                                                                  \
            exit(EXIT_FAILURE);                                                                    \
    } while (0)

#define log_debug(fmt, ...)    LOG(LL_DEBUG, "", fmt __VA_OPT__(, ) __VA_ARGS__)
#define log_info(fmt, ...)     LOG(LL_INFO, "", fmt __VA_OPT__(, ) __VA_ARGS__)
#define log_warning(fmt, ...)  LOG(LL_WARNING, "", fmt __VA_OPT__(, ) __VA_ARGS__)
#define log_error(fmt, ...)    LOG(LL_ERROR, "", fmt __VA_OPT__(, ) __VA_ARGS__)
#define log_critical(fmt, ...) LOG(LL_CRITICAL, "", fmt __VA_OPT__(, ) __VA_ARGS__)
