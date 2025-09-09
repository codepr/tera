#pragma once

#include "types.h"

int bin_write_u8(uint8 *buf, uint8 val);
uint8 bin_read_u8(const uint8 *const buf);
int bin_write_u16(uint8 *buf, uint16 val);
int16 bin_read_i16(const uint8 *const buf);
int bin_write_i16(uint8 *buf, int16 val);
uint16 bin_read_u16(const uint8 *const buf);
int bin_write_u32(uint8 *buf, uint32 val);
uint32 bin_read_u32(const uint8 *const buf);
int bin_write_i32(uint8 *buf, int32 val);
int32 bin_read_i32(const uint8 *buf);
int bin_write_i64(uint8 *buf, int64 val);
int64 bin_read_i64(const uint8 *const buf);
uint64 bin_read_u64(const uint8 *const buf);
int bin_write_f64(uint8 *buf, float64 val);
float64 bin_read_f64(const uint8 *buf);
