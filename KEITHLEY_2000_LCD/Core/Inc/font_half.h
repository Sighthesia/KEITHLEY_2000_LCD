#pragma once

#include <stdint.h>

#define FONT_HALF_WIDTH 32u
#define FONT_HALF_HEIGHT 56u
#define FONT_HALF_BYTES_PER_ROW 4u
#define FONT_HALF_BYTES_PER_GLYPH 224u
#define FONT_HALF_CHAR_COUNT 3u
#define FONT_HALF_BASELINE 47u
#define FONT_HALF_SYM_MICRO 0u
#define FONT_HALF_SYM_DEGREE 1u
#define FONT_HALF_SYM_OHM 2u
#define FONT_HALF_SYM_COUNT 3u

/* Bitmap for character c (1bpp, MSB first), or NULL if c is not
 * in the charset. The data lives in Flash; the caller does not
 * own it. */
const uint8_t *font_half_bitmap(char c);

/* Bitmap for one of the FONT_HALF_SYM_* symbols, or NULL for an unknown
 * symbol id. Data lives in Flash; the caller does not own it. */
const uint8_t *font_half_symbol_bitmap(uint8_t sym);

uint16_t font_half_width(void);
uint16_t font_half_height(void);
