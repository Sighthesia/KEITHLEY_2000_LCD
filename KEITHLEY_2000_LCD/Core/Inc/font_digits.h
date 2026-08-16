#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rif_reader.h"

/* RIF digit tiles preserve the former 64x128 layout without retaining their
 * 38 KiB bitmap source in MCU Flash. */
#define FONT_DIGIT_WIDTH 64u
#define FONT_DIGIT_HEIGHT 128u
#define FONT_DIGIT_BASELINE 103u
#define FONT_DIGIT_SYM_MICRO 0u
#define FONT_DIGIT_SYM_DEGREE 1u
#define FONT_DIGIT_SYM_OHM 2u
#define FONT_DIGIT_SYM_COUNT 3u

bool font_digit_rif_code(char c, uint32_t *kind, uint16_t *code);
bool font_digit_rif_symbol_code(uint8_t symbol, uint32_t *kind,
                                uint16_t *code);
