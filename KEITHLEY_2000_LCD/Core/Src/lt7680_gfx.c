#include "lt7680_gfx.h"
#include "font_text.h"

static lt7680_panel_t s_panel;
static lt7680_flash_b7_probe_t s_flash_b7_probe = {
    0u, 0u, 0u, LT7680_ERR_BUS, LT7680_ERR_BUS
};
static lt7680_flash_spi_snapshot_t s_flash_spi_snapshot = {
    0u, 0u, 0u, 0u, 0u, 0u, LT7680_ERR_BUS
};
static lt7680_flash_fifo_probe_t s_flash_fifo_probe;
static lt7680_flash_jedec_probe_t s_flash_jedec_probe = {
    0u, {0u, 0u, 0u, 0u}, LT7680_ERR_BUS
};
static lt7680_flash_header_probe_t s_flash_header_probe = {
    0u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
         0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}, LT7680_ERR_BUS
};

/* LT7680A-R register map. Display timings follow the values reverse-engineered
 * from the original V16 display firmware (see DEBUG_NOTES / panel protocol). */
#define LT7680_REG_CTRL 0x00u
#define LT7680_REG_PCLK_PLL_1 0x05u
#define LT7680_REG_PCLK_PLL_2 0x06u
#define LT7680_REG_MCLK_PLL_1 0x07u
#define LT7680_REG_MCLK_PLL_2 0x08u
#define LT7680_REG_CCLK_PLL_1 0x09u
#define LT7680_REG_CCLK_PLL_2 0x0Au
#define LT7680_REG_HOST_IF 0x01u
#define LT7680_REG_MEM_CFG 0x02u
#define LT7680_REG_GFX_MODE 0x03u
#define LT7680_REG_PANEL_CFG 0x10u
#define LT7680_REG_DISPLAY_CTRL 0x12u
#define LT7680_REG_SIGNAL_POLARITY 0x13u
#define LT7680_REG_HDWR 0x14u
#define LT7680_REG_HDWFTR 0x15u
#define LT7680_REG_HNDR 0x16u
#define LT7680_REG_HNDFTR 0x17u
#define LT7680_REG_HSTR 0x18u
#define LT7680_REG_HPWR 0x19u
#define LT7680_REG_VDHR0 0x1Au
#define LT7680_REG_VDHR1 0x1Bu
#define LT7680_REG_VNDR0 0x1Cu
#define LT7680_REG_VNDR1 0x1Du
#define LT7680_REG_VSTR 0x1Eu
#define LT7680_REG_VPWR 0x1Fu
#define LT7680_REG_SDRAM_CFG0 0xE0u
#define LT7680_REG_SDRAM_CFG1 0xE1u
#define LT7680_REG_SDRAM_REFRESH0 0xE2u
#define LT7680_REG_SDRAM_REFRESH1 0xE3u
#define LT7680_REG_SDRAM_CTRL 0xE4u
#define LT7680_REG_DCR0 0x67u
#define LT7680_REG_GE_SPT 0x68u
#define LT7680_REG_GE_EPT 0x6Cu
#define LT7680_REG_DCR1 0x76u
#define LT7680_REG_GE_RAD 0x77u
#define LT7680_REG_GE_CPT 0x7Bu
#define LT7680_REG_CURH 0x5Fu  /* Graphic R/W X coordinate (13-bit, lo then hi) */
#define LT7680_REG_CURV 0x61u  /* Graphic R/W Y coordinate (13-bit, lo then hi) */
#define LT7680_REG_MRWDP 0x04u /* Memory Data R/W Port: pixel writes go here */
#define LT7680_REG_MISA0 0x20u /* Main Image Start Address (4 bytes) */
#define LT7680_REG_MIW0 0x24u  /* Main Image Width (14-bit, lo then hi) */
#define LT7680_REG_MWULX0 0x26u /* Main Window Upper-Left X (13-bit) */
#define LT7680_REG_MWULY0 0x28u /* Main Window Upper-Left Y (13-bit) */
#define LT7680_REG_CVSSA0 0x50u /* Canvas Start Address (4 bytes) */
#define LT7680_REG_CVS_IMWTH0 0x54u /* Canvas Image Width (14-bit) */
#define LT7680_REG_AWUL_X0 0x56u /* Active Window Upper-Left X (13-bit) */
#define LT7680_REG_AWUL_Y0 0x58u /* Active Window Upper-Left Y (13-bit) */
#define LT7680_REG_AW_WTH0 0x5Au /* Active Window Width (14-bit) */
#define LT7680_REG_AW_HT0 0x5Cu  /* Active Window Height (14-bit) */
#define LT7680_REG_AW_COLOR 0x5Eu /* Canvas addressing mode + color depth */
#define LT7680_REG_FGCR 0xD2u
#define LT7680_REG_FGCG 0xD3u
#define LT7680_REG_FGCB 0xD4u

#define LT7680_REG_BTE_CTRL0 0x90u
#define LT7680_REG_BTE_CTRL1 0x91u
#define LT7680_REG_BTE_COLR 0x92u
#define LT7680_REG_BTE_S0_STR 0x93u
#define LT7680_REG_BTE_S0_WTH 0x97u
#define LT7680_REG_BTE_S0_X 0x99u
#define LT7680_REG_BTE_S0_Y 0x9Bu
#define LT7680_REG_BTE_DT_STR 0xA7u
#define LT7680_REG_BTE_DT_WTH 0xABu
#define LT7680_REG_BTE_DT_X 0xADu
#define LT7680_REG_BTE_DT_Y 0xAFu
#define LT7680_REG_BTE_SIZE 0xB1u
#define LT7680_REG_SFL_CTRL 0xB7u
#define LT7680_REG_SPIDR 0xB8u
#define LT7680_REG_SPIMCR2 0xB9u
#define LT7680_REG_SPIMSR 0xBAu
#define LT7680_REG_SPI_DIV 0xBBu
#define LT7680_REG_DMA_CTRL 0xB6u
#define LT7680_REG_DMA_SSTR 0xBCu
#define LT7680_REG_DMA_DX 0xC0u
#define LT7680_REG_DMA_DY 0xC2u
#define LT7680_REG_DMA_WTH 0xC6u
#define LT7680_REG_DMA_HIGH 0xC8u
#define LT7680_REG_DMA_SWTH 0xCAu

/* Keep page 1 on a 1 MiB boundary. MISA/CVSSA are four separate register
 * writes on the SPI bus; this alignment makes the first three bytes remain
 * zero when switching from page 0, so the visible address changes only on the
 * final byte instead of exposing intermediate addresses during the update. */
#define LT7680_CANVAS_PAGE_BYTES 0x00100000u

/* GE draw ops: LT768x DS V4.2 + Levetop LT768_Lib / RAiO Ra8876_Lite.
 * Filled rectangles go through DCR1 (REG[76h]): bit7=start, bit6=fill,
 * bit[5:4]=10b rectangle; DCR0 (REG[67h]) only handles line/triangle. */
#define LT7680_DCR1_RECT_FILL 0xE0u

#define LT7680_SPI_MASTER 0x02u
/* W25Q supports modes 0 and 3. The board's RIF bring-up sequence is mode 0;
 * keep CPOL/CPHA at the conservative reference setting while probing U5. */
#define LT7680_SPI_CTRL_READ_ACTIVE 0x1Cu
#define LT7680_SPI_CTRL_IDLE 0x0Cu
#define LT7680_SPI_STATUS_TX_EMPTY 0x80u
#define LT7680_SPI_STATUS_TX_FULL  0x40u
#define LT7680_SPI_STATUS_RX_EMPTY 0x20u
#define LT7680_SPI_STATUS_OVERFLOW 0x08u
#define LT7680_SPI_DIVISOR_SAFE 0x0Fu
#define LT7680_SFL_CTRL_SFCS1_DMA 0xC0u
#define LT7680_SPI_CTRL_DMA 0x3Cu
/* SFCS0, host FIFO access, 24-bit address, standard 03h/9Fh command mode.
 * The LT7680A-R keeps the host-FIFO bit set in B7; 0x40 is also the value used
 * by the vendor RA8876/LT768x drivers for the external SFCS0 device. */
#define LT7680_SFL_CTRL_RAW_DEFAULT 0x40u

/* forward decls (defined after lt7680_gfx_clear / lt7680_gfx_fill_rect) */
static lt7680_status_t set_fg_color16(uint16_t rgb565);
static lt7680_status_t wait_2d_idle(void);
static lt7680_status_t wr32le(uint8_t reg, uint32_t val);
static lt7680_status_t wr13(uint8_t reg, uint16_t value);
static lt7680_status_t wr16le(uint8_t reg, uint16_t val);

/* 4.58" bar panel: 320x960. V16-derived RGB timings (REG[14]-[1F]):
 * H_BACK=80, H_FRONT=16, H_SYNC=16, V_BACK=10, V_FRONT=12, V_SYNC=3.
 * Register encoding: H values (reg+1)*8, V values reg+1, matching V16. */
#define LT7680_H_BACK_PORCH 80u
#define LT7680_H_FRONT_PORCH 16u
#define LT7680_H_SYNC 16u
#define LT7680_V_BACK_PORCH 10u
#define LT7680_V_FRONT_PORCH 12u
#define LT7680_V_SYNC 3u

static lt7680_status_t write_reg(uint8_t reg, uint8_t value)
{
    return lt7680_write_reg(reg, value);
}

static lt7680_status_t flash_wait(uint8_t mask, uint8_t asserted)
{
    uint16_t i;
    uint8_t status;

    for (i = 0u; i < 2000u; i++) {
        lt7680_status_t st = lt7680_read_reg(LT7680_REG_SPIMSR, &status);
        if (st != LT7680_OK) {
            return st;
        }
        if ((status & LT7680_SPI_STATUS_OVERFLOW) != 0u) {
            (void)write_reg(LT7680_REG_SPIMSR, LT7680_SPI_STATUS_OVERFLOW);
            return LT7680_ERR_BUS;
        }
        if ((status & mask) == asserted) {
            return LT7680_OK;
        }
    }
    return LT7680_ERR_TIMEOUT;
}

static lt7680_status_t flash_read_fifo(uint8_t *value)
{
    lt7680_status_t st = flash_wait(LT7680_SPI_STATUS_RX_EMPTY, 0u);
    if (st != LT7680_OK) {
        return st;
    }
    return lt7680_read_reg(LT7680_REG_SPIDR, value);
}

static lt7680_status_t flash_begin_select(uint8_t select)
{
    uint8_t host_if;
    uint8_t b7_readback;
    lt7680_status_t st;
    /* Defensively reset SFL_CTRL to the documented raw-read default (SF0,
     * text mode, 24-bit address) in case the power-on display boot loader
     * left the controller in DMA/font mode. The host sends 03h/9Fh itself,
     * so no SFL_CTRL command-code setup is required. Do this before enabling
     * the host SPI-master bit; some LT7680 revisions gate B7 writes by mode. */
    s_flash_b7_probe.attempted = 1u;
    s_flash_b7_probe.requested = (uint8_t)(LT7680_SFL_CTRL_RAW_DEFAULT |
                                           (select != 0u ? 0x80u : 0u));
    s_flash_b7_probe.readback = 0u;
    s_flash_b7_probe.write_status = LT7680_ERR_BUS;
    s_flash_b7_probe.read_status = LT7680_ERR_BUS;
    st = write_reg(LT7680_REG_SFL_CTRL, s_flash_b7_probe.requested);
    s_flash_b7_probe.write_status = st;
    if (st != LT7680_OK) {
        return st;
    }
    s_flash_b7_probe.read_status = lt7680_read_reg(LT7680_REG_SFL_CTRL,
                                                   &b7_readback);
    if (s_flash_b7_probe.read_status == LT7680_OK) {
        s_flash_b7_probe.readback = b7_readback;
    }
    st = lt7680_read_reg(LT7680_REG_HOST_IF, &host_if);
    if (st != LT7680_OK) {
        return st;
    }
    st = write_reg(LT7680_REG_HOST_IF, (uint8_t)(host_if | LT7680_SPI_MASTER));
    if (st != LT7680_OK) {
        return st;
    }
    st = write_reg(LT7680_REG_SPI_DIV, LT7680_SPI_DIVISOR_SAFE);
    if (st != LT7680_OK) {
        return st;
    }
    return write_reg(LT7680_REG_SPIMCR2,
                     (uint8_t)(LT7680_SPI_CTRL_READ_ACTIVE |
                               (select != 0u ? 0x20u : 0u)));
}

static lt7680_status_t flash_begin(void)
{
    /* U5 is wired to SFCS1 on this board. Keep SFCS0 available only for the
     * read-only chip-select diagnostic. */
    return flash_begin_select(1u);
}

void lt7680_flash_get_b7_probe(lt7680_flash_b7_probe_t *probe)
{
    if (probe != 0) {
        *probe = s_flash_b7_probe;
    }
}

static lt7680_status_t flash_snapshot_spi_regs(lt7680_flash_spi_snapshot_t *probe)
{
    lt7680_status_t st;

    if (probe == 0) {
        return LT7680_ERR_PARAM;
    }
    probe->attempted = 1u;
    probe->status = LT7680_ERR_BUS;
    st = lt7680_read_reg(LT7680_REG_SFL_CTRL, &probe->b7);
    if (st != LT7680_OK) {
        probe->status = st;
        return st;
    }
    st = lt7680_read_reg(0xB6u, &probe->b6);
    if (st != LT7680_OK) {
        probe->status = st;
        return st;
    }
    st = lt7680_read_reg(LT7680_REG_SPIMCR2, &probe->b9);
    if (st != LT7680_OK) {
        probe->status = st;
        return st;
    }
    st = lt7680_read_reg(LT7680_REG_SPIMSR, &probe->ba);
    if (st != LT7680_OK) {
        probe->status = st;
        return st;
    }
    st = lt7680_read_reg(LT7680_REG_SPI_DIV, &probe->bb);
    if (st != LT7680_OK) {
        probe->status = st;
        return st;
    }
    probe->status = LT7680_OK;
    return LT7680_OK;
}

void lt7680_flash_get_spi_snapshot(lt7680_flash_spi_snapshot_t *probe)
{
    if (probe != 0) {
        *probe = s_flash_spi_snapshot;
    }
}

void lt7680_flash_get_fifo_probe(lt7680_flash_fifo_probe_t *probe)
{
    if (probe != 0) {
        *probe = s_flash_fifo_probe;
    }
}

void lt7680_flash_get_jedec_probe(lt7680_flash_jedec_probe_t *probe)
{
    if (probe != 0) {
        *probe = s_flash_jedec_probe;
    }
}

void lt7680_flash_get_header_probe(lt7680_flash_header_probe_t *probe)
{
    if (probe != 0) {
        *probe = s_flash_header_probe;
    }
}

static lt7680_status_t flash_push_and_drain_raw(const uint8_t *tx, uint8_t count,
                                                 uint8_t discard, uint8_t *data,
                                                 uint8_t *raw)
{
    uint8_t i;
    lt7680_status_t st;

    if (count == 0u || count > 16u || discard > count) {
        return LT7680_ERR_PARAM;
    }
    s_flash_fifo_probe.attempted = 1u;
    for (i = 0u; i < count; i++) {
        uint8_t spi_sr;
        if (lt7680_read_reg(LT7680_REG_SPIMSR, &spi_sr) == LT7680_OK) {
            s_flash_fifo_probe.last_status = spi_sr;
            if ((spi_sr & LT7680_SPI_STATUS_TX_FULL) != 0u) {
                s_flash_fifo_probe.full_count++;
            }
        } else {
            s_flash_fifo_probe.status_err = 1u;
        }
        st = write_reg(LT7680_REG_SPIDR, tx[i]);
        if (st != LT7680_OK) {
            return st;
        }
    }
    /* SPIMSR IDLE is interrupt-mask dependent on this controller. RX_EMPTY
     * supplies the required per-byte completion check while draining below. */
    st = flash_wait(LT7680_SPI_STATUS_TX_EMPTY, LT7680_SPI_STATUS_TX_EMPTY);
    if (st != LT7680_OK) {
        return st;
    }
    for (i = 0u; i < count; i++) {
        uint8_t value;
        st = flash_read_fifo(&value);
        if (st != LT7680_OK) {
            return st;
        }
        if (raw != 0) {
            raw[i] = value;
        }
        if (i >= discard) {
            data[i - discard] = value;
        }
    }
    return LT7680_OK;
}

static lt7680_status_t flash_push_and_drain(const uint8_t *tx, uint8_t count,
                                            uint8_t discard, uint8_t *data)
{
    return flash_push_and_drain_raw(tx, count, discard, data, 0);
}

lt7680_status_t lt7680_flash_read(uint32_t address, uint8_t *data,
                                  uint16_t length)
{
    uint8_t command[16];
    uint16_t remaining;
    lt7680_status_t st;
    uint8_t *read_start = data;
    uint8_t header_read = (uint8_t)(address == 0u && length >= 16u);

    if (data == 0 || length == 0u || address > 0x00FFFFFFu ||
        length > 0x01000000u - address) {
        return LT7680_ERR_PARAM;
    }
    if (header_read != 0u) {
        uint8_t i;
        s_flash_header_probe.attempted = 1u;
        s_flash_header_probe.status = LT7680_ERR_BUS;
        for (i = 0u; i < 16u; i++) {
            s_flash_header_probe.raw[i] = 0u;
        }
    }
    if (address == 0u && length >= 16u) {
        (void)flash_snapshot_spi_regs(&s_flash_spi_snapshot);
    }
    st = flash_begin();
    if (st == LT7680_OK) {
        command[0] = 0x03u;
        command[1] = (uint8_t)(address >> 16);
        command[2] = (uint8_t)(address >> 8);
        command[3] = (uint8_t)address;
        remaining = length;
        while (remaining > 0u) {
            uint8_t chunk = remaining > 12u ? 12u : (uint8_t)remaining;
            uint8_t i;
            for (i = 0u; i < chunk; i++) {
                command[4u + i] = 0u;
            }
            st = flash_push_and_drain(command, (uint8_t)(4u + chunk), 4u, data);
            if (st != LT7680_OK) {
                break;
            }
            data += chunk;
            remaining = (uint16_t)(remaining - chunk);
            while (remaining > 0u) {
                chunk = remaining > 16u ? 16u : (uint8_t)remaining;
                for (i = 0u; i < chunk; i++) {
                    command[i] = 0u;
                }
                st = flash_push_and_drain(command, chunk, 0u, data);
                if (st != LT7680_OK) {
                    break;
                }
                data += chunk;
                remaining = (uint16_t)(remaining - chunk);
            }
        }
    }
    (void)write_reg(LT7680_REG_SPIMCR2, LT7680_SPI_CTRL_IDLE);
    if (header_read != 0u) {
        uint8_t i;
        s_flash_header_probe.status = st;
        if (st == LT7680_OK) {
            for (i = 0u; i < 16u; i++) {
                s_flash_header_probe.raw[i] = read_start[i];
            }
        }
        if (s_flash_spi_snapshot.attempted != 0u) {
            (void)flash_snapshot_spi_regs(&s_flash_spi_snapshot);
        }
    }
    return st;
}

lt7680_status_t lt7680_flash_dma_to_sdram(uint32_t flash_address,
                                          uint32_t sdram_base,
                                          uint16_t width_bytes,
                                          uint16_t height,
                                          uint16_t destination_stride_pixels)
{
    uint64_t flash_end;
    uint64_t sdram_end;
    uint32_t row_bytes;
    uint16_t block_width_pixels;
    uint32_t saved_cvssa;
    uint16_t saved_canvas_stride;
    lt7680_status_t st;
    lt7680_status_t restore_st = LT7680_OK;
    uint8_t value;

    row_bytes = (uint32_t)destination_stride_pixels * 2u;
    if (flash_address > 0x00FFFFFFu || width_bytes == 0u ||
        (width_bytes & 1u) != 0u || height == 0u ||
        destination_stride_pixels == 0u || width_bytes > row_bytes) {
        return LT7680_ERR_PARAM;
    }
    flash_end = (uint64_t)flash_address + (uint32_t)width_bytes * height;
    sdram_end = (uint64_t)sdram_base +
                (uint32_t)(height - 1u) * row_bytes + width_bytes;
    if (flash_end > 0x01000000u ||
        sdram_base < 0x00200000u || sdram_end < sdram_base ||
        sdram_end > 0x01000000u) {
        return LT7680_ERR_PARAM;
    }
    block_width_pixels = (uint16_t)(width_bytes / 2u);

    st = lt7680_read_reg(LT7680_REG_CVSSA0, &value);
    if (st != LT7680_OK) return st;
    saved_cvssa = value;
    st = lt7680_read_reg((uint8_t)(LT7680_REG_CVSSA0 + 1u), &value);
    if (st != LT7680_OK) return st;
    saved_cvssa |= (uint32_t)value << 8;
    st = lt7680_read_reg((uint8_t)(LT7680_REG_CVSSA0 + 2u), &value);
    if (st != LT7680_OK) return st;
    saved_cvssa |= (uint32_t)value << 16;
    st = lt7680_read_reg((uint8_t)(LT7680_REG_CVSSA0 + 3u), &value);
    if (st != LT7680_OK) return st;
    saved_cvssa |= (uint32_t)value << 24;
    st = lt7680_read_reg(LT7680_REG_CVS_IMWTH0, &value);
    if (st != LT7680_OK) return st;
    saved_canvas_stride = value;
    st = lt7680_read_reg((uint8_t)(LT7680_REG_CVS_IMWTH0 + 1u), &value);
    if (st != LT7680_OK) return st;
    saved_canvas_stride |= (uint16_t)value << 8;

    st = wr32le(LT7680_REG_CVSSA0, sdram_base);
    if (st == LT7680_OK) st = wr13(LT7680_REG_CVS_IMWTH0,
                                   destination_stride_pixels);
    if (st == LT7680_OK) st = write_reg(LT7680_REG_SFL_CTRL,
                                        LT7680_SFL_CTRL_SFCS1_DMA);
    if (st == LT7680_OK) st = write_reg(LT7680_REG_SPIMCR2,
                                        LT7680_SPI_CTRL_DMA);
    if (st == LT7680_OK) st = write_reg(LT7680_REG_SPI_DIV,
                                        LT7680_SPI_DIVISOR_SAFE);
    if (st == LT7680_OK) st = wr32le(LT7680_REG_DMA_SSTR, flash_address);
    if (st == LT7680_OK) st = wr16le(LT7680_REG_DMA_DX, 0u);
    if (st == LT7680_OK) st = wr16le(LT7680_REG_DMA_DY, 0u);
    /* DMAW_WTH and DMA_SWTH are image widths in pixels. The public API keeps
     * width_bytes because the SFCS1 source is a packed RGB565 byte stream;
     * destination_stride_pixels remains the canvas stride written above. */
    if (st == LT7680_OK) st = wr16le(LT7680_REG_DMA_WTH,
                                     block_width_pixels);
    if (st == LT7680_OK) st = wr16le(LT7680_REG_DMA_HIGH, height);
    if (st == LT7680_OK) st = wr16le(LT7680_REG_DMA_SWTH,
                                      block_width_pixels);
    /* Release the host-controlled Flash transaction before DMA takes over
     * SFCS. Keeping the manual active-CS value here can leave DMA_START stuck
     * while the serial-flash state machine waits for ownership. */
    if (st == LT7680_OK) st = write_reg(LT7680_REG_SPIMCR2,
                                        LT7680_SPI_CTRL_IDLE);
    if (st == LT7680_OK) st = write_reg(LT7680_REG_DMA_CTRL, 0x01u);
    if (st == LT7680_OK) st = wait_2d_idle();

    if (wr32le(LT7680_REG_CVSSA0, saved_cvssa) != LT7680_OK) {
        restore_st = LT7680_ERR_BUS;
    }
    if (wr13(LT7680_REG_CVS_IMWTH0, saved_canvas_stride) != LT7680_OK) {
        restore_st = LT7680_ERR_BUS;
    }
    if (write_reg(LT7680_REG_SPIMCR2, LT7680_SPI_CTRL_IDLE) != LT7680_OK) {
        restore_st = LT7680_ERR_BUS;
    }
    /* Keep U5 selected on SFCS1 without issuing any Flash write command. */
    if (write_reg(LT7680_REG_SFL_CTRL, LT7680_SFL_CTRL_SFCS1_DMA) != LT7680_OK) {
        restore_st = LT7680_ERR_BUS;
    }
    return st != LT7680_OK ? st : restore_st;
}

lt7680_status_t lt7680_flash_dma_read_snapshot(
    lt7680_flash_dma_snapshot_t *snapshot)
{
    uint8_t value;
    lt7680_status_t st = LT7680_OK;
    uint8_t reg;

    if (snapshot == 0) {
        return LT7680_ERR_PARAM;
    }
    snapshot->b6 = 0u;
    snapshot->b7 = 0u;
    snapshot->b9 = 0u;
    snapshot->ba = 0u;
    snapshot->bb = 0u;
    snapshot->cvssa = 0u;
    snapshot->canvas_stride = 0u;
    snapshot->core_status = 0u;
    snapshot->sdram_status = 0u;
    for (reg = 0u; reg < sizeof(snapshot->bc_cb); reg++)
        snapshot->bc_cb[reg] = 0u;
    snapshot->status = LT7680_ERR_BUS;
    st = lt7680_read_reg(LT7680_REG_DMA_CTRL, &snapshot->b6);
    if (st != LT7680_OK) goto fail;
    st = lt7680_read_reg(LT7680_REG_SFL_CTRL, &snapshot->b7);
    if (st != LT7680_OK) goto fail;
    st = lt7680_read_reg(LT7680_REG_SPIMCR2, &snapshot->b9);
    if (st != LT7680_OK) goto fail;
    st = lt7680_read_reg(LT7680_REG_SPIMSR, &snapshot->ba);
    if (st != LT7680_OK) goto fail;
    st = lt7680_read_reg(LT7680_REG_SPI_DIV, &snapshot->bb);
    if (st != LT7680_OK) goto fail;
    for (reg = LT7680_REG_DMA_SSTR; reg <= (LT7680_REG_DMA_SWTH + 1u);
         reg++) {
        st = lt7680_read_reg(reg, &snapshot->bc_cb[reg - LT7680_REG_DMA_SSTR]);
        if (st != LT7680_OK) goto fail;
    }
    snapshot->cvssa = 0u;
    for (reg = 0u; reg < 4u; reg++) {
        st = lt7680_read_reg((uint8_t)(LT7680_REG_CVSSA0 + reg), &value);
        if (st != LT7680_OK) goto fail;
        snapshot->cvssa |= (uint32_t)value << (8u * reg);
    }
    st = lt7680_read_reg(LT7680_REG_CVS_IMWTH0, &value);
    if (st != LT7680_OK) goto fail;
    snapshot->canvas_stride = value;
    st = lt7680_read_reg((uint8_t)(LT7680_REG_CVS_IMWTH0 + 1u), &value);
    if (st != LT7680_OK) goto fail;
    snapshot->canvas_stride |= (uint16_t)value << 8;
    st = lt7680_read_status(&snapshot->core_status);
    if (st != LT7680_OK) goto fail;
    st = lt7680_read_reg(LT7680_REG_SDRAM_CTRL, &snapshot->sdram_status);
    if (st != LT7680_OK) goto fail;
    snapshot->status = LT7680_OK;
    return LT7680_OK;

fail:
    snapshot->status = st;
    return st;
}

lt7680_status_t lt7680_flash_read_jedec_id(uint8_t id[3])
{
    const uint8_t command[4] = {0x9Fu, 0u, 0u, 0u};
    lt7680_status_t st;

    if (id == 0) {
        return LT7680_ERR_PARAM;
    }
    s_flash_jedec_probe.attempted = 1u;
    s_flash_jedec_probe.status = LT7680_ERR_BUS;
    st = flash_begin();
    if (st == LT7680_OK) {
        st = flash_push_and_drain_raw(command, sizeof(command), 1u, id,
                                      s_flash_jedec_probe.raw);
    }
    s_flash_jedec_probe.status = st;
    (void)write_reg(LT7680_REG_SPIMCR2, LT7680_SPI_CTRL_IDLE);
    return st;
}

lt7680_status_t lt7680_flash_jedec_diagnostic(lt7680_flash_jedec_diag_t *diag)
{
    static const uint8_t command[4] = {0x9Fu, 0u, 0u, 0u};
    lt7680_status_t st;
    uint8_t i;

    if (diag == 0) {
        return LT7680_ERR_PARAM;
    }
    for (i = 0u; i < 4u; i++) {
        diag->batch_raw[i] = 0u;
        diag->step_raw[i] = 0u;
    }
    diag->batch_status = LT7680_ERR_BUS;
    diag->step_status = LT7680_ERR_BUS;

    st = flash_begin();
    if (st == LT7680_OK) {
        diag->batch_status = flash_push_and_drain_raw(
            command, sizeof(command), 0u, diag->batch_raw, 0);
    } else {
        diag->batch_status = st;
    }
    (void)write_reg(LT7680_REG_SPIMCR2, LT7680_SPI_CTRL_IDLE);

    st = flash_begin();
    if (st == LT7680_OK) {
        diag->step_status = LT7680_OK;
        for (i = 0u; i < 4u; i++) {
            st = write_reg(LT7680_REG_SPIDR, command[i]);
            if (st != LT7680_OK) {
                diag->step_status = st;
                break;
            }
            st = flash_wait(LT7680_SPI_STATUS_TX_EMPTY,
                            LT7680_SPI_STATUS_TX_EMPTY);
            if (st != LT7680_OK) {
                diag->step_status = st;
                break;
            }
            st = flash_read_fifo(&diag->step_raw[i]);
            if (st != LT7680_OK) {
                diag->step_status = st;
                break;
            }
        }
    } else {
        diag->step_status = st;
    }
    (void)write_reg(LT7680_REG_SPIMCR2, LT7680_SPI_CTRL_IDLE);
    return diag->batch_status != LT7680_OK ? diag->batch_status
                                           : diag->step_status;
}

lt7680_status_t lt7680_flash_cs_diagnostic(lt7680_flash_cs_diag_t *diag)
{
    static const uint8_t command[4] = {0x9Fu, 0u, 0u, 0u};
    lt7680_status_t st;

    if (diag == 0) {
        return LT7680_ERR_PARAM;
    }
    for (uint8_t i = 0u; i < 4u; i++) {
        diag->sfcs0_raw[i] = 0u;
        diag->sfcs1_raw[i] = 0u;
    }
    diag->sfcs0_status = LT7680_ERR_BUS;
    diag->sfcs1_status = LT7680_ERR_BUS;

    st = flash_begin_select(0u);
    if (st == LT7680_OK) {
        diag->sfcs0_status = flash_push_and_drain_raw(
            command, sizeof(command), 0u, diag->sfcs0_raw, 0);
    } else {
        diag->sfcs0_status = st;
    }
    (void)write_reg(LT7680_REG_SPIMCR2, LT7680_SPI_CTRL_IDLE);

    st = flash_begin_select(1u);
    if (st == LT7680_OK) {
        diag->sfcs1_status = flash_push_and_drain_raw(
            command, sizeof(command), 0u, diag->sfcs1_raw, 0);
    } else {
        diag->sfcs1_status = st;
    }
    (void)write_reg(LT7680_REG_SPIMCR2, LT7680_SPI_CTRL_IDLE);
    return diag->sfcs0_status != LT7680_OK ? diag->sfcs0_status
                                           : diag->sfcs1_status;
}

/* Read-modify-write, as the original V16 firmware does: preserve untouched
 * bits and only clear/set the ones the display config needs. */
static lt7680_status_t rmw_reg(uint8_t reg, uint8_t set_bits,
                               uint8_t clear_bits)
{
    lt7680_status_t st;
    uint8_t val;

    st = lt7680_read_reg(reg, &val);
    if (st != LT7680_OK) {
        return st;
    }
    val = (uint8_t)((val & ~clear_bits) | set_bits);
    return write_reg(reg, val);
}

static lt7680_status_t configure_pll(void)
{
    lt7680_status_t st;

    /* PLL values from original V16 firmware (REG[05..0A]):
     * PCLK C1/C2 = 0x8A/0x19, MCLK C1/C2 = 0x8A/0x64, CCLK C1/C2 = 0x8A/0x64. */
    st = write_reg(LT7680_REG_PCLK_PLL_1, 0x8Au);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_PCLK_PLL_2, 0x19u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_MCLK_PLL_1, 0x8Au);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_MCLK_PLL_2, 0x64u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_CCLK_PLL_1, 0x8Au);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_CCLK_PLL_2, 0x64u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_CTRL, 0x80u);
    if (st != LT7680_OK) return st;
    return lt7680_wait_ready(100u);
}

static lt7680_status_t configure_sdram(void)
{
    lt7680_status_t st;
    uint8_t ready;
    uint32_t waited;

    /* V16 values: CFG0=0x29, CFG1=0x03 (CAS 3), refresh=0x01E6, CTRL=0x01. */
    st = write_reg(LT7680_REG_SDRAM_CTRL, 0x04u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_SDRAM_CFG0, 0x29u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_SDRAM_CFG1, 0x03u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_SDRAM_REFRESH0, 0xE6u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_SDRAM_REFRESH1, 0x01u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_SDRAM_CTRL, 0x01u);
    if (st != LT7680_OK) return st;
    st = lt7680_delay_ms(1u);
    if (st != LT7680_OK) return st;

    for (waited = 0u; waited < 100u; waited++) {
        st = lt7680_read_reg(LT7680_REG_SDRAM_CTRL, &ready);
        if (st != LT7680_OK) return st;
        if ((ready & 0x01u) != 0u) {
            return LT7680_OK;
        }
        st = lt7680_delay_ms(1u);
        if (st != LT7680_OK) return st;
    }
    return LT7680_ERR_TIMEOUT;
}

static lt7680_status_t configure_panel(void)
{
    const uint16_t hdwr = (s_panel.width / 8u) - 1u;
    const uint16_t hndr = (LT7680_H_BACK_PORCH / 8u) - 1u;
    const uint16_t hstr = (LT7680_H_FRONT_PORCH / 8u) - 1u;
    const uint16_t hpwr = (LT7680_H_SYNC / 8u) - 1u;
    const uint16_t vdhr = s_panel.height - 1u;
    const uint16_t vndr = LT7680_V_BACK_PORCH - 1u;
    lt7680_status_t st;

    /* REG[01h]: V16 RMW = clear bit4, set bit3 (write 1), then set bit0
     * (write 2): clear mask 0x10, set 0x09.  bit3 = 18-bit RGB output
     * matching R2-R7/G2-G7/B2-B7, bit0 = normal operation (not sleep). */
    st = rmw_reg(LT7680_REG_HOST_IF, 0x09u, 0x10u);
    if (st != LT7680_OK) return st;
    /* REG[02h]: retain the V16 memory access mode. This bit controls the
     * controller's display-RAM fetch path as well as CPU access; changing it
     * to 0x00 can leave GE writes readable while Main Image output is blank. */
    st = rmw_reg(LT7680_REG_MEM_CFG, 0x40u, 0x86u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_GFX_MODE, 0x00u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_PANEL_CFG, 0x04u);
    if (st != LT7680_OK) return st;
    /* REG[13h]: V16 clears bits7-5 via RMW => HS/VS low active, DE high
     * active.  Preserve the low bits rather than overwriting them. */
    st = rmw_reg(LT7680_REG_SIGNAL_POLARITY, 0x00u, 0xE0u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_HDWR, (uint8_t)hdwr);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_HDWFTR, 0x00u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_HNDR, (uint8_t)hndr);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_HNDFTR, 0x00u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_HSTR, (uint8_t)hstr);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_HPWR, (uint8_t)hpwr);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_VDHR0, (uint8_t)vdhr);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_VDHR1, (uint8_t)(vdhr >> 8));
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_VNDR0, (uint8_t)vndr);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_VNDR1, (uint8_t)(vndr >> 8));
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_VSTR, LT7680_V_FRONT_PORCH - 1u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_VPWR, LT7680_V_SYNC - 1u);
    if (st != LT7680_OK) return st;
    /* REG[12h]: V16 init via RMW clears bit7 (PCLK), sets bit3 (scan dir),
     * clears bit4 (HSCAN L->R) and bits0-2 (PDATA RGB order). */
    return rmw_reg(LT7680_REG_DISPLAY_CTRL, 0x08u, 0x97u);
}

/* Configure the Main / Canvas / Active windows for image output from Display
 * RAM (datasheet V4.2, memory-write procedure and section 10.2).  The color
 * bar test pattern is an internal generator that bypasses these registers, so
 * turning bit5 of REG[12h] off shows the canvas through the main window;
 * with all of them at their default 0 the picture collapses to a sliver.
 * Block (X-Y) addressing, 16bpp canvas, windows covering the whole panel. */
static lt7680_status_t wr32le(uint8_t reg, uint32_t val);
static lt7680_status_t wr13(uint8_t reg, uint16_t value);
static lt7680_status_t configure_windows(void)
{
    lt7680_status_t st;

    /* Main image: starts at Display RAM address 0, one panel-wide row,
     * displayed at panel origin (0,0). */
    st = wr32le(LT7680_REG_MISA0, 0u);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_MIW0, s_panel.width);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_MWULX0, 0u);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_MWULY0, 0u);
    if (st != LT7680_OK) return st;

    /* Canvas: same region as the main image (row stride = panel width). */
    st = wr32le(LT7680_REG_CVSSA0, 0u);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_CVS_IMWTH0, s_panel.width);
    if (st != LT7680_OK) return st;

    /* Active window = whole panel (region the host may write). */
    st = wr13(LT7680_REG_AWUL_X0, 0u);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_AWUL_Y0, 0u);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_AW_WTH0, s_panel.width);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_AW_HT0, s_panel.height);
    if (st != LT7680_OK) return st;

    /* Canvas addressing: block (X-Y) mode, 16bpp memory R/W.
     * REG[5Eh] bit[1:0] in Block mode: 00=8bpp, 01=16bpp, 1x=24bpp
     * (LT768x_DS V4.2). 0x01 is 16bpp; 0x02/0x03 would select 24bpp.
     * The AP-Note flash demo writes 0x02 only because that picture is 24bpp. */
    return write_reg(LT7680_REG_AW_COLOR, 0x01u);
}

lt7680_status_t lt7680_gfx_init(const lt7680_panel_t *panel)
{
    lt7680_status_t st;

    if (panel == 0 || panel->width == 0u || panel->height == 0u ||
        panel->bpp != 16u || (panel->width & 0x07u) != 0u) {
        return LT7680_ERR_PARAM;
    }
    s_panel = *panel;

    st = configure_pll();
    if (st != LT7680_OK) return st;
    st = configure_sdram();
    if (st != LT7680_OK) return st;
    st = configure_panel();
    if (st != LT7680_OK) return st;
    return configure_windows();
}

lt7680_status_t lt7680_gfx_select_canvas_page(uint8_t page)
{
    if (page > 1u || s_panel.width == 0u || s_panel.height == 0u) {
        return LT7680_ERR_PARAM;
    }
    return wr32le(LT7680_REG_CVSSA0,
                  (uint32_t)page * LT7680_CANVAS_PAGE_BYTES);
}

lt7680_status_t lt7680_gfx_present_page(uint8_t page)
{
    if (page > 1u || s_panel.width == 0u || s_panel.height == 0u) {
        return LT7680_ERR_PARAM;
    }
    /* MISA is latched by the display fetch path; changing it does not blank
     * the panel and therefore presents the already-complete page atomically. */
    return wr32le(LT7680_REG_MISA0,
                  (uint32_t)page * LT7680_CANVAS_PAGE_BYTES);
}

lt7680_status_t lt7680_gfx_copy_page(uint8_t source_page, uint8_t target_page)
{
    lt7680_status_t st;

    if (source_page > 1u || target_page > 1u || source_page == target_page ||
        s_panel.width == 0u || s_panel.height == 0u) {
        return LT7680_ERR_PARAM;
    }
    /* RGB565 memory-copy-with-ROP: ROP C copies S0; F is whiteness. */
    st = write_reg(LT7680_REG_BTE_CTRL1, 0xC2u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_BTE_COLR, 0x25u);
    if (st != LT7680_OK) return st;
    st = wr32le(LT7680_REG_BTE_S0_STR,
                (uint32_t)source_page * LT7680_CANVAS_PAGE_BYTES);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_S0_WTH, s_panel.width);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_S0_X, 0u);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_S0_Y, 0u);
    if (st != LT7680_OK) return st;
    st = wr32le(LT7680_REG_BTE_DT_STR,
                (uint32_t)target_page * LT7680_CANVAS_PAGE_BYTES);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_DT_WTH, s_panel.width);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_DT_X, 0u);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_DT_Y, 0u);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_SIZE, s_panel.width);
    if (st != LT7680_OK) return st;
    st = wr13((uint8_t)(LT7680_REG_BTE_SIZE + 2u), s_panel.height);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_BTE_CTRL0, 0x10u);
    if (st != LT7680_OK) return st;
    return wait_2d_idle();
}

/* Copy an RGB565 rectangle from an absolute SDRAM source to the given canvas
 * page with the verified BTE ROP. The source is any packed tile area (e.g. a
 * pre-transposed glyph library at 0x200000+), so source and destination
 * strides are independent. Both strides are in pixels. */
lt7680_status_t lt7680_gfx_blit(uint8_t canvas_page, uint32_t src_addr,
                                uint16_t src_stride, uint16_t dst_x,
                                uint16_t dst_y, uint16_t w, uint16_t h)
{
    lt7680_status_t st;

    if (canvas_page > 1u || s_panel.width == 0u || s_panel.height == 0u ||
        src_addr > 0x00FFFFFFu || w == 0u || h == 0u ||
        (uint32_t)dst_x + w > s_panel.width ||
        (uint32_t)dst_y + h > s_panel.height) {
        return LT7680_ERR_PARAM;
    }
    st = write_reg(LT7680_REG_BTE_CTRL1, 0xC2u);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_BTE_COLR, 0x25u);
    if (st != LT7680_OK) return st;
    st = wr32le(LT7680_REG_BTE_S0_STR, src_addr);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_S0_WTH, src_stride);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_S0_X, 0u);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_S0_Y, 0u);
    if (st != LT7680_OK) return st;
    st = wr32le(LT7680_REG_BTE_DT_STR,
                (uint32_t)canvas_page * LT7680_CANVAS_PAGE_BYTES);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_DT_WTH, s_panel.width);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_DT_X, dst_x);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_DT_Y, dst_y);
    if (st != LT7680_OK) return st;
    st = wr13(LT7680_REG_BTE_SIZE, w);
    if (st != LT7680_OK) return st;
    st = wr13((uint8_t)(LT7680_REG_BTE_SIZE + 2u), h);
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_BTE_CTRL0, 0x10u);
    if (st != LT7680_OK) return st;
    return wait_2d_idle();
}

lt7680_status_t lt7680_gfx_show_color_bars(void)
{
    /* V16 display-on adds bit6 (0x40) on top of the init value (bit3 set,
     * bit7/bit4/bits0-2 clear).  Color-bar test pattern is bit5 (0x20). */
    return rmw_reg(LT7680_REG_DISPLAY_CTRL, 0x68u, 0x97u);
}

lt7680_status_t lt7680_gfx_clear(uint16_t rgb565)
{
    lt7680_rect_t full;

    if (s_panel.width == 0u || s_panel.height == 0u) {
        return LT7680_ERR_PARAM;
    }
    full.x = 0u;
    full.y = 0u;
    full.w = s_panel.width;
    full.h = s_panel.height;
    return lt7680_gfx_fill_rect(&full, rgb565);
}

lt7680_status_t lt7680_gfx_fill_rect(const lt7680_rect_t *rect, uint16_t rgb565)
{
    lt7680_status_t st;

    if (rect == 0) {
        return LT7680_ERR_PARAM;
    }

    st = set_fg_color16(rgb565);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr32le(LT7680_REG_GE_SPT, ((uint32_t)rect->x & 0x1FFFu) |
                                   (((uint32_t)rect->y & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    /* End point is exclusive: end = (x + w, y + h). Levetop full-screen fills
     * use (0,0,width,height); the active-window clip makes both conventions
     * equivalent for a full-panel clear. */
    st = wr32le(LT7680_REG_GE_EPT, (((uint32_t)rect->x + rect->w) & 0x1FFFu) |
                                   ((((uint32_t)rect->y + rect->h) & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    st = write_reg(LT7680_REG_DCR1, LT7680_DCR1_RECT_FILL);
    if (st != LT7680_OK) {
        return st;
    }
    return wait_2d_idle();
}

lt7680_status_t lt7680_gfx_draw_rect(const lt7680_rect_t *rect, uint16_t rgb565)
{
    (void)rect;
    (void)rgb565;
    return LT7680_ERR_PARAM;
}

/* RGB565 -> LT7680 16bpp foreground color: R = FGCR[7:3], G = FGCG[7:2],
 * B = FGCB[7:3] (datasheet V4.2). */
static lt7680_status_t set_fg_color16(uint16_t rgb565)
{
    uint8_t r = (uint8_t)((rgb565 >> 11) & 0x1Fu);
    uint8_t g = (uint8_t)((rgb565 >> 5)  & 0x3Fu);
    uint8_t b = (uint8_t)(rgb565          & 0x1Fu);
    lt7680_status_t st = write_reg(LT7680_REG_FGCR, (uint8_t)(r << 3));
    if (st != LT7680_OK) return st;
    st = write_reg(LT7680_REG_FGCG, (uint8_t)(g << 2));
    if (st != LT7680_OK) return st;
    return write_reg(LT7680_REG_FGCB, (uint8_t)(b << 3));
}

/* Write a 32-bit little-endian value to 4 consecutive registers. */
static lt7680_status_t wr32le(uint8_t reg, uint32_t val)
{
    for (uint8_t i = 0u; i < 4u; i++) {
        lt7680_status_t st = write_reg((uint8_t)(reg + i),
                                       (uint8_t)(val >> (8u * i)));
        if (st != LT7680_OK) {
            return st;
        }
    }
    return LT7680_OK;
}

/* Write a 16-bit little-endian DMA field. Unlike graphic coordinates, these
 * fields use the full register pair width. */
static lt7680_status_t wr16le(uint8_t reg, uint16_t val)
{
    lt7680_status_t st = write_reg(reg, (uint8_t)val);
    if (st != LT7680_OK) {
        return st;
    }
    return write_reg((uint8_t)(reg + 1u), (uint8_t)(val >> 8));
}

/* Write a 13-bit value to a coordinate register pair (lo at reg, hi at
 * reg+1), as used by the graphic R/W cursor. */
static lt7680_status_t wr13(uint8_t reg, uint16_t value)
{
    lt7680_status_t st = write_reg(reg, (uint8_t)(value & 0xFFu));
    if (st != LT7680_OK) {
        return st;
    }
    return write_reg((uint8_t)(reg + 1u), (uint8_t)(value >> 8));
}

/* Direct pixel write: position the graphic R/W cursor, point the data port
 * at Display RAM (REG[04h]), then push one 16bpp pixel (low byte first) to
 * the cursor address.  Skipping the MRWDP address write would send the two
 * bytes into the last-addressed register (CURV) instead of memory. */
lt7680_status_t lt7680_gfx_set_pixel(uint16_t x, uint16_t y, uint16_t rgb565)
{
    lt7680_status_t st;

    if (x >= s_panel.width || y >= s_panel.height) {
        return LT7680_ERR_PARAM;
    }
    st = wr13(LT7680_REG_CURH, x);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(LT7680_REG_CURV, y);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_select_reg(LT7680_REG_MRWDP);
    if (st != LT7680_OK) {
        return st;
    }
    {
        uint8_t pixel[2];
        pixel[0] = (uint8_t)(rgb565 & 0xFFu);
        pixel[1] = (uint8_t)(rgb565 >> 8);
        return lt7680_write_data(pixel, 2u);
    }
}

/* Read back one 16bpp pixel from Display RAM at the graphic R/W cursor.
 * Selecting MRWDP on each byte read keeps the read targeting the data port
 * even if the first read does not auto-advance the cursor on this silicon. */
lt7680_status_t lt7680_gfx_peek_pixel(uint16_t x, uint16_t y, uint16_t *rgb565)
{
    lt7680_status_t st;
    uint8_t lo = 0u, hi = 0u;

    if (x >= s_panel.width || y >= s_panel.height || rgb565 == 0) {
        return LT7680_ERR_PARAM;
    }
    st = wr13(LT7680_REG_CURH, x);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(LT7680_REG_CURV, y);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_select_reg(LT7680_REG_MRWDP);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_read_reg(LT7680_REG_MRWDP, &lo);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_read_reg(LT7680_REG_MRWDP, &hi);
    if (st != LT7680_OK) {
        return st;
    }
    *rgb565 = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    return LT7680_OK;
}

lt7680_status_t lt7680_gfx_draw_text(uint16_t x, uint16_t y, const char *text,
                                     uint16_t fg, uint16_t bg)
{
    uint16_t cx;

    if (text == 0) {
        return LT7680_ERR_PARAM;
    }
    cx = x;
    while (*text != '\0') {
        const uint8_t *bitmap = font_text_bitmap(*text);
        uint16_t row;
        uint16_t col;

        if (bitmap == 0) {
            return LT7680_ERR_PARAM;
        }
        for (row = 0u; row < FONT_TEXT_HEIGHT; row++) {
            for (col = 0u; col < FONT_TEXT_WIDTH; col++) {
                const uint8_t *bits = bitmap + row * FONT_TEXT_BYTES_PER_ROW;
                uint16_t color = (bits[col >> 3] & (uint8_t)(0x80u >> (col & 7u)))
                                     != 0u ? fg : bg;
                lt7680_status_t st = lt7680_gfx_set_pixel(
                    (uint16_t)(cx + col), (uint16_t)(y + row), color);
                if (st != LT7680_OK) {
                    return st;
                }
            }
        }
        cx = (uint16_t)(cx + FONT_TEXT_WIDTH);
        text++;
    }
    return LT7680_OK;
}

/* Wait for the geometry engine to finish (status bit 0x08 = CORE_BUSY). */
static lt7680_status_t wait_2d_idle(void)
{
    uint8_t status = 0;
    for (uint16_t i = 0u; i < 1000u; i++) {
        lt7680_status_t st = lt7680_read_status(&status);
        if (st != LT7680_OK) {
            return st;
        }
        if ((status & LT7680_STATUS_CORE_BUSY) == 0u) {
            return LT7680_OK;
        }
    }
    return LT7680_ERR_TIMEOUT;
}

lt7680_status_t lt7680_gfx_draw_line(int16_t x0, int16_t y0, int16_t x1,
                                     int16_t y1, uint16_t rgb565)
{
    lt7680_status_t st;

    if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0) {
        return LT7680_ERR_PARAM;
    }
    if ((uint32_t)x0 >= s_panel.width || (uint32_t)x1 >= s_panel.width ||
        (uint32_t)y0 >= s_panel.height || (uint32_t)y1 >= s_panel.height) {
        return LT7680_ERR_PARAM;
    }

    st = set_fg_color16(rgb565);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr32le(LT7680_REG_GE_SPT, ((uint32_t)x0 & 0x1FFFu) |
                                   (((uint32_t)y0 & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    st = wr32le(LT7680_REG_GE_EPT, ((uint32_t)x1 & 0x1FFFu) |
                                   (((uint32_t)y1 & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    st = write_reg(LT7680_REG_DCR0, 0x80u);  /* bit7 start, line, no fill */
    if (st != LT7680_OK) {
        return st;
    }
    return wait_2d_idle();
}

lt7680_status_t lt7680_gfx_draw_polyline(const int16_t *xy, uint16_t n_points,
                                         uint16_t rgb565)
{
    if (xy == 0 || n_points < 2u) {
        return LT7680_ERR_PARAM;
    }
    for (uint16_t i = 0u; i + 1u < n_points; i++) {
        lt7680_status_t st = lt7680_gfx_draw_line(xy[2u * i],
                                                  xy[2u * i + 1u],
                                                  xy[2u * i + 2u],
                                                  xy[2u * i + 3u],
                                                  rgb565);
        if (st != LT7680_OK) {
            return st;
        }
    }
    return LT7680_OK;
}

lt7680_status_t lt7680_gfx_draw_circle(int16_t xc, int16_t yc, int16_t r,
                                       uint16_t rgb565)
{
    lt7680_status_t st;

    if (xc < 0 || yc < 0 || r < 0) {
        return LT7680_ERR_PARAM;
    }
    /* Circle spans [xc-r, xc+r] x [yc-r, yc+r] inclusive, so both edges
     * must fit in [0, width-1] x [0, height-1]. */
    if (xc - r < 0 || yc - r < 0 ||
        (uint32_t)xc + (uint32_t)r >= s_panel.width ||
        (uint32_t)yc + (uint32_t)r >= s_panel.height) {
        return LT7680_ERR_PARAM;
    }

    st = set_fg_color16(rgb565);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr32le(LT7680_REG_GE_RAD, ((uint32_t)r & 0x1FFFu) |
                                   (((uint32_t)r & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    st = wr32le(LT7680_REG_GE_CPT, ((uint32_t)xc & 0x1FFFu) |
                                   (((uint32_t)yc & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    st = write_reg(LT7680_REG_DCR1, 0x80u);  /* bit7 start, circle, no fill */
    if (st != LT7680_OK) {
        return st;
    }
    return wait_2d_idle();
}
