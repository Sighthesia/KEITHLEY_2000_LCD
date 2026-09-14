#pragma once

#include <stdbool.h>
#include <stdint.h>

bool lt7680_validate_2d_source(uint32_t address, uint16_t stride_pixels,
                               uint16_t width_pixels, uint16_t height,
                               uint32_t sdram_limit);
bool lt7680_validate_2d_destination(uint32_t base, uint16_t stride_pixels,
                                    uint16_t x, uint16_t y,
                                    uint16_t width_pixels, uint16_t height,
                                    uint32_t sdram_limit);
