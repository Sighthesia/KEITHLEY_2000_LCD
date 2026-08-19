#include "rif_tile_cache.h"

#include <stddef.h>

#define RIF_TILE_CACHE_STAGING_BASE 0x00200000u
#define RIF_TILE_CACHE_BASE         0x00300000u
#define RIF_TILE_CACHE_SDRAM_END   0x01000000u
#define RIF_TILE_CACHE_CANVAS_END  0x00200000u

#define RIF_TILE_CACHE_LARGE_WIDTH  128u
#define RIF_TILE_CACHE_LARGE_HEIGHT 64u
#define RIF_TILE_CACHE_HALF_WIDTH   64u
#define RIF_TILE_CACHE_HALF_HEIGHT  32u
#define RIF_TILE_CACHE_LARGE_BYTES \
    (RIF_TILE_CACHE_LARGE_WIDTH * RIF_TILE_CACHE_LARGE_HEIGHT * 2u)
#define RIF_TILE_CACHE_HALF_BYTES \
    (RIF_TILE_CACHE_HALF_WIDTH * RIF_TILE_CACHE_HALF_HEIGHT * 2u)
#define RIF_TILE_CACHE_SLOT_COUNT 8u

static rif_tile_cache_entry_t s_entries[RIF_TILE_CACHE_SLOT_COUNT];
static uint32_t s_next_address;

static uint32_t cache_tile_bytes(const rif_tile_t *tile)
{
    if (tile->width == 64u && tile->height == 128u && tile->stride == 128u)
        return RIF_TILE_CACHE_LARGE_BYTES;
    if (tile->width == 32u && tile->height == 64u && tile->stride == 64u)
        return RIF_TILE_CACHE_HALF_BYTES;
    return 0u;
}

static lt7680_status_t validate_slot(uint32_t address, uint32_t bytes)
{
    uint64_t end = (uint64_t)address + bytes;

    if (address < RIF_TILE_CACHE_BASE || address < RIF_TILE_CACHE_CANVAS_END ||
        end < address || end > RIF_TILE_CACHE_SDRAM_END)
        return LT7680_ERR_PARAM;
    return LT7680_OK;
}

void rif_tile_cache_init(void)
{
    uint8_t i;

    s_next_address = RIF_TILE_CACHE_BASE;
    for (i = 0u; i < RIF_TILE_CACHE_SLOT_COUNT; i++) {
        s_entries[i].kind = 0u;
        s_entries[i].code = 0u;
        s_entries[i].address = 0u;
        s_entries[i].stride = 0u;
        s_entries[i].width = 0u;
        s_entries[i].height = 0u;
        s_entries[i].ready = 0u;
    }
}

lt7680_status_t rif_tile_cache_prepare(uint32_t kind, uint16_t code,
                                        const rif_tile_t *tile,
                                        rif_tile_cache_entry_t *entry)
{
    uint8_t i;
    uint32_t bytes;
    uint16_t width;
    uint16_t height;

    if (tile == NULL || entry == NULL || kind == 0u)
        return LT7680_ERR_PARAM;

    for (i = 0u; i < RIF_TILE_CACHE_SLOT_COUNT; i++) {
        if (s_entries[i].ready != 0u && s_entries[i].kind == kind &&
            s_entries[i].code == code) {
            *entry = s_entries[i];
            return LT7680_OK;
        }
    }

    bytes = cache_tile_bytes(tile);
    if (bytes == 0u)
        return LT7680_ERR_PARAM;

    width = tile->width == 64u ? RIF_TILE_CACHE_LARGE_WIDTH :
                                  RIF_TILE_CACHE_HALF_WIDTH;
    height = tile->height == 128u ? RIF_TILE_CACHE_LARGE_HEIGHT :
                                    RIF_TILE_CACHE_HALF_HEIGHT;
    if (validate_slot(s_next_address, bytes) != LT7680_OK) {
        entry->ready = 0u;
        return LT7680_ERR_PARAM;
    }

    /* The current LT7680 API has no validated BTE direction probe, no absolute
     * SDRAM graphic-write primitive, and no bounded SDRAM readback. Do not
     * infer the direction register semantics or publish an unverified tile. */
    (void)kind;
    (void)code;
    (void)width;
    (void)height;
    (void)RIF_TILE_CACHE_STAGING_BASE;
    entry->ready = 0u;
    return LT7680_ERR_UNSUPPORTED;
}
