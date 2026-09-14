#include <assert.h>
#include <stddef.h>

#include "lt7680_gfx.h"
#include "rif_tile_cache.h"

static int fail_flash;
static int fail_base;
static int fail_width;
static int fail_pixels;
static uint32_t base_calls[4];
static uint16_t width_calls[4];
static unsigned base_count;
static unsigned width_count;
static uint32_t last_base;
static uint16_t last_width;

lt7680_status_t lt7680_flash_dma_read_snapshot(
    lt7680_flash_dma_snapshot_t *snapshot)
{
    snapshot->cvssa = 0x00123456u;
    snapshot->canvas_stride = 320u;
    return LT7680_OK;
}

lt7680_status_t lt7680_flash_read(uint32_t address, uint8_t *data,
                                  uint16_t length)
{
    (void)address;
    if (fail_flash) {
        fail_flash = 0;
        return LT7680_ERR_BUS;
    }
    for (uint16_t i = 0u; i < length; i++)
        data[i] = 0u;
    return LT7680_OK;
}

lt7680_status_t lt7680_gfx_set_canvas_base(uint32_t address)
{
    if (base_count < 4u)
        base_calls[base_count] = address;
    last_base = address;
    base_count++;
    if (fail_base && address != 0x00123456u) {
        fail_base = 0;
        return LT7680_ERR_BUS;
    }
    return LT7680_OK;
}

lt7680_status_t lt7680_gfx_set_canvas_width(uint16_t width)
{
    if (width_count < 4u)
        width_calls[width_count] = width;
    last_width = width;
    width_count++;
    if (fail_width && width != 320u) {
        fail_width = 0;
        return LT7680_ERR_BUS;
    }
    return LT7680_OK;
}

lt7680_status_t lt7680_gfx_write_pixels(uint16_t x, uint16_t y,
                                        const uint16_t *pixels, uint16_t count)
{
    (void)x;
    (void)y;
    (void)pixels;
    (void)count;
    if (fail_pixels) {
        fail_pixels = 0;
        return LT7680_ERR_BUS;
    }
    return LT7680_OK;
}

static uint32_t zero_crc(uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < len; i++) {
        crc ^= 0u;
        for (unsigned bit = 0u; bit < 8u; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static rif_tile_t tile(void)
{
    rif_tile_t result = {0u, 4096u, 32u, 64u, 64u, 0u, 0u, zero_crc(4096u)};
    return result;
}

static void reset_mocks(void)
{
    fail_flash = 0;
    fail_base = 0;
    fail_width = 0;
    fail_pixels = 0;
    base_count = 0u;
    width_count = 0u;
    last_base = 0u;
    last_width = 0u;
}

static void assert_failure(int *failure)
{
    rif_tile_cache_entry_t entry = {0u, 0u, 0u, 0u, 0u, 0u, 1u};
    rif_tile_t input = tile();
    lt7680_status_t status;

    rif_tile_cache_init();
    reset_mocks();
    *failure = 1;
    status = rif_tile_cache_prepare(1u, 1u, &input, &entry);
    assert(status == LT7680_ERR_BUS);
    assert(entry.ready == 0u);
    assert(last_base == 0x00123456u);
    assert(last_width == 320u);
}

int main(void)
{
    assert_failure(&fail_flash);
    assert_failure(&fail_base);
    assert_failure(&fail_width);
    assert_failure(&fail_pixels);

    {
        rif_tile_cache_entry_t entry = {0u, 0u, 0u, 0u, 0u, 0u, 1u};
        rif_tile_t input = tile();
        rif_tile_cache_init();
        reset_mocks();
        input.crc32 ^= 1u;
        assert(rif_tile_cache_prepare(1u, 2u, &input, &entry) == LT7680_ERR_BUS);
        assert(entry.ready == 0u);
        assert(last_base == 0x00123456u);
        assert(last_width == 320u);
    }

    {
        rif_tile_cache_entry_t entry = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
        rif_tile_t input = tile();
        rif_tile_cache_init();
        reset_mocks();
        assert(rif_tile_cache_prepare(1u, 3u, &input, &entry) == LT7680_OK);
        assert(entry.ready == 1u);
        assert(entry.address == 0x00300000u);
        assert(entry.stride == 64u);
        assert(entry.width == 64u);
        assert(entry.height == 32u);
    }
    return 0;
}
