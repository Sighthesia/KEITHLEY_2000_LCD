#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Max content length of the numeric and unit parts of a split reading.
 * NUL termination needs one more byte, so callers must provide buffers of at
 * least READING_SPLIT_MAX_NUM + 1 and READING_SPLIT_MAX_UNIT + 1 bytes
 * (e.g. char num[32], char unit[16]). */
#define READING_SPLIT_MAX_NUM 31u
#define READING_SPLIT_MAX_UNIT 15u

/* Split a reading ASCII field into the leading numeric prefix (charset
 * { '0'-'9', '.', '+', '-', 'E', 'e' }) copied to num, and the remainder (if
 * any) copied to unit. Both outputs are NUL-terminated and bounded by the
 * constants above, so writes never overflow. NULL inputs are handled safely
 * (outputs are zeroed/emptied). */
void reading_split(const char *ascii, uint8_t len, char *num, uint8_t *num_len,
                   char *unit, uint8_t *unit_len);

/* Special readings, recognized by exact byte match: "OVERFLOW" -> 1 (render
 * red), "----" -> 2 (render grey), anything else -> 0. Returns true when a
 * special reading matched and stores the code in *special (when non-NULL). */
bool reading_is_special(const char *num, uint8_t len, uint8_t *special);
bool reading_normalize_unit(const char *unit, uint8_t len, char *out,
                            uint8_t out_size);
