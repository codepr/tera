#pragma once

#include "types.h"
#include <time.h>

void init_boot_time(void);
uint32 current_millis_relative(void);
time_t current_seconds(void);
int64 current_millis(void);
int64 current_micros(void);
int64 current_nanos(void);
int clocktime(struct timespec *ts);
float64 timespec_seconds(struct timespec *ts);
int64 timespan_seconds(int64 mul, const char *ts);
int64 datetime_seconds(const char *datetime_str);
