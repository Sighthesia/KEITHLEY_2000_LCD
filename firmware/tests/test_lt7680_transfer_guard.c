#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "lt7680_gfx.h"

#define REG_CCR 0x01u
#define REG_SDRCR 0xE4u
#define REG_DMA_CTRL 0xB6u
#define REG_BTE_CTRL0 0x90u

typedef struct {
    bool selected;
    uint8_t command;
    uint8_t phase;
    uint8_t register_address;
    uint8_t registers[256];
    uint32_t dma_ctrl_writes;
    uint32_t bte_ctrl0_writes;
} mock_bus_t;

static mock_bus_t s_bus;

static void mock_cs(bool level)
{
    s_bus.selected = !level;
    if (!s_bus.selected) {
        s_bus.command = 0u;
        s_bus.phase = 0u;
    }
}

static void mock_reset(bool level)
{
    (void)level;
}

static bool mock_spi_failed(void)
{
    return false;
}

static void mock_delay(uint32_t ms)
{
    (void)ms;
}

static uint8_t mock_spi_xfer(uint8_t byte)
{
    if (s_bus.phase == 0u) {
        s_bus.command = byte;
        s_bus.phase = 1u;
        return 0u;
    }

    if (s_bus.command == LT7680_SPI_CMD_WRITE_REG) {
        if (s_bus.phase == 1u) {
            s_bus.register_address = byte;
            s_bus.phase = 2u;
        }
        return 0u;
    }

    if (s_bus.command == LT7680_SPI_CMD_WRITE_DATA) {
        s_bus.registers[s_bus.register_address] = byte;
        if (s_bus.register_address == REG_DMA_CTRL) {
            s_bus.dma_ctrl_writes++;
        }
        if (s_bus.register_address == REG_BTE_CTRL0) {
            s_bus.bte_ctrl0_writes++;
        }
        return 0u;
    }

    if (s_bus.command == LT7680_SPI_CMD_READ_REG) {
        if (s_bus.register_address == REG_CCR) {
            return 0x80u;
        }
        if (s_bus.register_address == REG_SDRCR) {
            return 0x01u;
        }
        return s_bus.registers[s_bus.register_address];
    }

    if (s_bus.command == LT7680_SPI_CMD_READ_STATUS) {
        return 0x04u;
    }

    return 0u;
}

static const lt7680_bus_io_t s_io = {
    .cs = mock_cs,
    .rst = mock_reset,
    .spi_xfer = mock_spi_xfer,
    .spi_failed = mock_spi_failed,
    .delay_ms = mock_delay,
};

static void mock_bus_init(void)
{
    s_bus = (mock_bus_t){0};
    s_bus.registers[REG_CCR] = 0x80u;
    s_bus.registers[REG_SDRCR] = 0x01u;
    lt7680_bus_init(&s_io);
}

static void reset_write_counts(void)
{
    s_bus.dma_ctrl_writes = 0u;
    s_bus.bte_ctrl0_writes = 0u;
}

int main(void)
{
    const lt7680_panel_t panel = {
        .width = 320u,
        .height = 960u,
        .bpp = 16u,
        .hsw = 8u,
        .hbp = 8u,
        .hfp = 8u,
        .vsw = 1u,
        .vbp = 1u,
        .vfp = 1u,
        .refresh_hz = 60u,
        .hsync_active_high = 0u,
        .vsync_active_high = 0u,
        .pclk_invert = 0u,
        .rgb_order = 0u,
        .dram_start = 0u,
    };

    mock_bus_init();
    assert(lt7680_gfx_init(&panel) == LT7680_OK);

    reset_write_counts();
    assert(lt7680_flash_dma_tile_to_canvas(0u, 0x00F80000u, 320u,
                                            0u, 892u, 128u, 68u) ==
           LT7680_ERR_PARAM);
    assert(s_bus.dma_ctrl_writes == 0u);
    assert(s_bus.bte_ctrl0_writes == 0u);

    reset_write_counts();
    assert(lt7680_gfx_blit(1u, 0x00FFF000u, 320u, 0u, 0u, 128u, 68u) ==
           LT7680_ERR_PARAM);
    assert(s_bus.dma_ctrl_writes == 0u);
    assert(s_bus.bte_ctrl0_writes == 0u);

    reset_write_counts();
    assert(lt7680_gfx_blit(1u, 0x00300000u, 79u, 0u, 0u, 80u, 44u) ==
           LT7680_ERR_PARAM);
    assert(s_bus.dma_ctrl_writes == 0u);
    assert(s_bus.bte_ctrl0_writes == 0u);

    return 0;
}
