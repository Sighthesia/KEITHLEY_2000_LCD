#include "rif_tile_cache.h"

#include "lt7680_gfx.h"

#include <stddef.h>

#define RIF_TILE_CACHE_STAGING_BASE 0x00200000u
#define RIF_TILE_CACHE_BASE         0x00300000u
#define RIF_TILE_CACHE_SDRAM_END   0x01000000u
#define RIF_TILE_CACHE_CANVAS_END  0x00200000u
#define RIF_TILE_CACHE_MAX_DMA_ROWS 10u

#define RIF_TILE_CACHE_LARGE_WIDTH  128u
#define RIF_TILE_CACHE_LARGE_HEIGHT 64u
#define RIF_TILE_CACHE_HALF_WIDTH   64u
#define RIF_TILE_CACHE_HALF_HEIGHT  32u
#define RIF_TILE_CACHE_LARGE_BYTES \
    (RIF_TILE_CACHE_LARGE_WIDTH * RIF_TILE_CACHE_LARGE_HEIGHT * 2u)
#define RIF_TILE_CACHE_HALF_BYTES \
    (RIF_TILE_CACHE_HALF_WIDTH * RIF_TILE_CACHE_HALF_HEIGHT * 2u)
#define RIF_TILE_CACHE_SLOT_COUNT 40u

static rif_tile_cache_entry_t s_entries[RIF_TILE_CACHE_SLOT_COUNT];
static uint32_t s_next_address;
static uint16_t s_chunk_pixels[RIF_TILE_CACHE_MAX_DMA_ROWS *
                               RIF_TILE_CACHE_LARGE_WIDTH];
static uint16_t s_line_pixels[RIF_TILE_CACHE_MAX_DMA_ROWS];

static uint32_t cache_tile_bytes(const rif_tile_t *tile, uint16_t *cache_width,
                                 uint16_t *cache_height)
{
    if (tile->width == 64u && tile->height == 128u && tile->stride == 128u)
    {
        *cache_width = RIF_TILE_CACHE_LARGE_WIDTH;
        *cache_height = RIF_TILE_CACHE_LARGE_HEIGHT;
        return RIF_TILE_CACHE_LARGE_BYTES;
    }
    if (tile->width == 32u && tile->height == 64u && tile->stride == 64u)
    {
        *cache_width = RIF_TILE_CACHE_HALF_WIDTH;
        *cache_height = RIF_TILE_CACHE_HALF_HEIGHT;
        return RIF_TILE_CACHE_HALF_BYTES;
    }
    return 0u;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        uint8_t b = data[i];
        uint8_t bit;

        crc ^= b;
        for (bit = 0u; bit < 8u; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
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
    uint32_t cache_bytes;
    uint16_t cache_width = 0u;
    uint16_t cache_height = 0u;
    uint16_t chunk_rows;
    uint32_t cache_address;
    uint16_t row;
    uint16_t col;
    uint32_t flash_address;
    uint16_t cache_row;
    uint32_t crc;
    lt7680_status_t st;
    lt7680_flash_dma_snapshot_t saved;

    if (tile == NULL || entry == NULL || kind == 0u)
        return LT7680_ERR_PARAM;

    /* The builder retargets CVSSA/CVS_IMWTH for off-screen writes; both must
     * be restored, otherwise every later GE fill wraps at the tile width. */
    st = lt7680_flash_dma_read_snapshot(&saved);
    if (st != LT7680_OK)
        return st;

    for (i = 0u; i < RIF_TILE_CACHE_SLOT_COUNT; i++) {
        if (s_entries[i].ready != 0u && s_entries[i].kind == kind &&
            s_entries[i].code == code) {
            *entry = s_entries[i];
            return LT7680_OK;
        }
    }

    cache_bytes = cache_tile_bytes(tile, &cache_width, &cache_height);
    if (cache_bytes == 0u)
        return LT7680_ERR_PARAM;
    if (validate_slot(s_next_address, cache_bytes) != LT7680_OK) {
        entry->ready = 0u;
        return LT7680_ERR_PARAM;
    }

    cache_address = s_next_address;
    crc = 0xFFFFFFFFu;
    for (row = 0u; row < tile->height; ) {
        chunk_rows = (uint16_t)(tile->height - row);
        if (chunk_rows > RIF_TILE_CACHE_MAX_DMA_ROWS)
            chunk_rows = RIF_TILE_CACHE_MAX_DMA_ROWS;

        flash_address = tile->offset + (uint32_t)row * tile->stride;
        st = lt7680_flash_read(flash_address,
                               (uint8_t *)s_chunk_pixels,
                               (uint16_t)(tile->stride * chunk_rows));
        if (st != LT7680_OK) {
            entry->ready = 0u;
            return st;
        }
        crc = crc32_update(crc, (const uint8_t *)s_chunk_pixels,
                           (uint32_t)tile->stride * chunk_rows);

        st = lt7680_gfx_set_canvas_base(cache_address);
        if (st != LT7680_OK) {
            entry->ready = 0u;
            return st;
        }
        st = lt7680_gfx_set_canvas_width(cache_width);
        if (st != LT7680_OK) {
            entry->ready = 0u;
            return st;
        }

        for (col = 0u; col < tile->width; col++) {
            for (cache_row = 0u; cache_row < chunk_rows; cache_row++) {
                uint32_t pixel_index = (uint32_t)cache_row * tile->stride + col * 2u;
                s_line_pixels[cache_row] = (uint16_t)s_chunk_pixels[pixel_index / 2u];
            }
            st = lt7680_gfx_write_pixels(row, col, s_line_pixels, chunk_rows);
            if (st != LT7680_OK) {
                entry->ready = 0u;
                return st;
            }
        }

        row = (uint16_t)(row + chunk_rows);
    }

    st = lt7680_gfx_set_canvas_base(saved.cvssa);
    if (st == LT7680_OK)
        st = lt7680_gfx_set_canvas_width(saved.canvas_stride);
    if (st != LT7680_OK) {
        entry->ready = 0u;
        return st;
    }

    if ((~crc) != tile->crc32) {
        entry->ready = 0u;
        return LT7680_ERR_BUS;
    }


    entry->ready = 0u;
    for (i = 0u; i < RIF_TILE_CACHE_SLOT_COUNT; i++) {
        if (s_entries[i].ready == 0u) {
            s_entries[i].kind = kind;
            s_entries[i].code = code;
            s_entries[i].address = s_next_address;
            s_entries[i].stride = cache_width;
            s_entries[i].width = cache_width;
            s_entries[i].height = cache_height;
            s_entries[i].ready = 1u;
            *entry = s_entries[i];
            s_next_address += cache_bytes;
            return LT7680_OK;
        }
    }

    entry->ready = 0u;
    return LT7680_ERR_BUSY;
}

lt7680_status_t rif_tile_cache_lookup(uint32_t kind, uint16_t code,
                                      rif_tile_cache_entry_t *entry)
{
    uint8_t i;

    if (entry == NULL || kind == 0u)
        return LT7680_ERR_PARAM;
    for (i = 0u; i < RIF_TILE_CACHE_SLOT_COUNT; i++) {
        if (s_entries[i].ready != 0u && s_entries[i].kind == kind &&
            s_entries[i].code == code) {
            *entry = s_entries[i];
            return LT7680_OK;
        }
    }
    entry->ready = 0u;
    return LT7680_ERR_UNSUPPORTED;
}
