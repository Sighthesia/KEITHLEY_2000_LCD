#include "lt7680_transfer_range.h"

#include <limits.h>

static bool range_end_is_valid(uint64_t end, uint32_t sdram_limit)
{
    return end <= (uint64_t)UINT32_MAX && end <= (uint64_t)sdram_limit;
}

bool lt7680_validate_2d_source(uint32_t address, uint16_t stride_pixels,
                               uint16_t width_pixels, uint16_t height,
                               uint32_t sdram_limit)
{
    uint64_t end;

    if (stride_pixels < width_pixels || width_pixels == 0u || height == 0u)
        return false;

    end = (uint64_t)address +
          (((uint64_t)(height - 1u) * stride_pixels + width_pixels) * 2u);
    return range_end_is_valid(end, sdram_limit);
}

bool lt7680_validate_2d_destination(uint32_t base, uint16_t stride_pixels,
                                    uint16_t x, uint16_t y,
                                    uint16_t width_pixels, uint16_t height,
                                    uint32_t sdram_limit)
{
    uint64_t end;

    if (stride_pixels < width_pixels || width_pixels == 0u || height == 0u)
        return false;

    end = (uint64_t)base +
          ((uint64_t)y + height - 1u) * stride_pixels * 2u +
          ((uint64_t)x + width_pixels) * 2u;
    return range_end_is_valid(end, sdram_limit);
}
