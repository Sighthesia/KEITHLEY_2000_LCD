#include "lt7680_gfx.h"
#include "font_text.h"

/* LT768x register map (datasheet V4.2). All register addresses are 8-bit. */
#define REG_SRR      0x00u  /* Software Reset Register */
#define REG_CCR      0x01u  /* Chip Configuration Register */
#define REG_MACR     0x02u  /* Memory Access Control Register */
#define REG_ICR      0x03u  /* Input Control Register */
#define REG_MRWDP    0x04u  /* Memory Data R/W Port */
#define REG_PPLLC1   0x05u  /* PCLK PLL Control 1 */
#define REG_PPLLC2   0x06u  /* PCLK PLL Control 2 */
#define REG_MPLLC1   0x07u  /* MCLK PLL Control 1 */
#define REG_MPLLC2   0x08u  /* MCLK PLL Control 2 */
#define REG_CPLLC1   0x09u  /* CCLK PLL Control 1 */
#define REG_CPLLC2   0x0Au  /* CCLK PLL Control 2 */
#define REG_MPWCTR   0x10u  /* Main/PIP Window Control */
#define REG_DPCR     0x12u  /* Display Configuration Register */
#define REG_PCSR     0x13u  /* Panel Scan Clock & Data Setting */
#define REG_HDWR     0x14u  /* Horizontal Display Width */
#define REG_HDWFTR   0x15u  /* Horizontal Display Width Fine Tune */
#define REG_HNDR     0x16u  /* Horizontal Non-Display Period */
#define REG_HNDFTR   0x17u  /* Horizontal Non-Display Period Fine Tune */
#define REG_HSTR     0x18u  /* HSYNC Start Position */
#define REG_HPWR     0x19u  /* HSYNC Pulse Width */
#define REG_VDHR     0x1Au  /* Vertical Display Height */
#define REG_VNDR     0x1Cu  /* Vertical Non-Display Period */
#define REG_VSTR     0x1Eu  /* VSYNC Start Position */
#define REG_VPWR     0x1Fu  /* VSYNC Pulse Width */
#define REG_MISA     0x20u  /* Main Image Start Address */
#define REG_MIW      0x24u  /* Main Image Width */
#define REG_MWULX    0x26u  /* Main Window Upper-Left X */
#define REG_MWULY    0x28u  /* Main Window Upper-Left Y */
#define REG_CVSSA    0x50u  /* Canvas Start Address */
#define REG_CVSIMWTH 0x54u  /* Canvas Image Width */
#define REG_AWULX    0x56u  /* Active Window Upper-Left X */
#define REG_AWULY    0x58u  /* Active Window Upper-Left Y */
#define REG_AWWTH    0x5Au  /* Active Window Width */
#define REG_AWHT     0x5Cu  /* Active Window Height */
#define REG_AWCOLOR  0x5Eu  /* Canvas & Active Window Color Depth */
#define REG_CURH     0x5Fu  /* Graphic R/W X Coordinate */
#define REG_CURV     0x61u  /* Graphic R/W Y Coordinate */
#define REG_CURHW    0x63u  /* Text Write X Coordinates */
#define REG_CURVW    0x65u  /* Text Write Y Coordinates */
#define REG_DCR0     0x67u  /* Draw Line/Triangle Control Register 0 */
#define REG_GE_SPT   0x68u  /* Geometry Engine Start Point (4 bytes) */
#define REG_GE_EPT   0x6Cu  /* Geometry Engine End Point (4 bytes) */
#define REG_DCR1     0x76u  /* Draw Square/Circle Control Register 1 */
#define REG_GE_RAD   0x77u  /* Geometry Engine Radius: major (lo/hi), minor */
#define REG_GE_CPT   0x7Bu  /* Geometry Engine Circle Center Point (4 bytes) */
#define REG_FGCR     0xD2u  /* Foreground Color - Red */
#define REG_FGCG     0xD3u  /* Foreground Color - Green */
#define REG_FGCB     0xD4u  /* Foreground Color - Blue */
#define REG_SDRAR    0xE0u  /* SDRAM Attribute Register */
#define REG_SDRMD    0xE1u  /* SDRAM Mode Register */
#define REG_SDRREF   0xE2u  /* SDRAM Auto Refresh Interval (E2=low, E3=high) */
#define REG_SDRCR    0xE4u  /* SDRAM Control Register */
#define REG_SFL_CTRL 0xB7u  /* Serial Flash controller configuration */
#define REG_SPIDR    0xB8u  /* Serial Flash SPI data FIFO */
#define REG_SPIMCR2  0xB9u  /* Serial Flash SPI master control */
#define REG_SPIMSR   0xBAu  /* Serial Flash SPI master status */
#define REG_SPI_DIV  0xBBu  /* Serial Flash SPI clock divisor */

/* Chip configuration bits (REG[01h]). */
#define CCR_TFT_16BIT   (0x02u << 3)  /* bit[4:3] = 10b: 16-bit TFT output */
#define CCR_SPI_MASTER  (0x01u << 1)  /* bit1: serial Flash/SPI master */

/* Main/PIP window control (REG[10h]). */
#define MPWCTR_MAIN_16BPP (0x01u << 2) /* bit[3:2] = 01b: 16bpp main image */
#define MPWCTR_SYNC_MODE  0x00u        /* bit0 = 0: DE + HSYNC + VSYNC */

/* Display configuration (REG[12h]). */
#define DPCR_DISPLAY_ON   (0x01u << 6)
#define DPCR_PCLK_INVERT  (0x01u << 7)

/* Canvas & active window color depth (REG[5Eh]).
 * bit[1:0] in Block mode (LT768x_DS V4.2): 00=8bpp, 01=16bpp, 1x=24bpp.
 * 0x01 is 16bpp; 0x02/0x03 select 24bpp (the AP-Note flash demo writes 0x02
 * only because that picture is 24bpp). */
#define AWCOLOR_BLOCK (0x00u << 2)  /* bit2 = 0: block (X-Y) addressing */
#define AWCOLOR_16BPP 0x01u         /* bit[1:0] = 01b: 16bpp */

/* Line/triangle control 0 (REG[67h]). */
#define DCR0_DRAW_FILL (0x01u << 5)  /* bit5: fill */
#define DCR0_DRAW_RECT (0x02u << 1)  /* bit[4:1] = 0010b: rectangle */
#define DCR0_DRAW_EN   (0x01u << 7)  /* bit7: start drawing */

/* Square/circle control 1 (REG[76h]). From Levetop LT768x AP-Note:
 * non-fill circle/ellipse = 0x80, fill circle = 0xC0,
 * non-fill square = 0xA0, fill square = 0xE0 (bit7 start, bit6 fill). */
#define DCR1_DRAW_EN    (0x01u << 7)

/* Fixed PLL targets. MCLK must match the SDRAM refresh reference
 * (REG[E3h:E2h] = 0x061A is given for MCLK = 100 MHz). */
#define PLL_CCLK_MHZ 100u
#define PLL_MCLK_MHZ 100u

static lt7680_panel_t s_panel;

#define SPI_CTRL_READ_ACTIVE 0x1Cu
#define SPI_CTRL_IDLE 0x0Cu
#define SPI_STATUS_TX_EMPTY 0x80u
#define SPI_STATUS_RX_EMPTY 0x20u
#define SPI_STATUS_OVERFLOW 0x08u
#define SPI_DIVISOR_SAFE 0x0Fu
/* Raw host reads push 03h/9Fh themselves through the FIFO, so SFL_CTRL is not
 * part of the read transaction. The documented raw-read default (SF0, text
 * mode, 24-bit address, 03h command) is 0x00; writing it defensively clears
 * any DMA-mode leftover from the power-on display boot loader. */
#define SFL_CTRL_RAW_DEFAULT 0x00u

static lt7680_status_t wr(uint8_t reg, uint8_t val)
{
    return lt7680_write_reg(reg, val);
}

static lt7680_status_t spi_wait(uint8_t mask, uint8_t asserted)
{
    uint16_t i;
    uint8_t status;

    for (i = 0u; i < 2000u; i++) {
        lt7680_status_t st = lt7680_read_reg(REG_SPIMSR, &status);
        if (st != LT7680_OK) {
            return st;
        }
        if ((status & SPI_STATUS_OVERFLOW) != 0u) {
            (void)wr(REG_SPIMSR, SPI_STATUS_OVERFLOW);
            return LT7680_ERR_BUS;
        }
        if ((status & mask) == asserted) {
            return LT7680_OK;
        }
    }
    return LT7680_ERR_TIMEOUT;
}

static lt7680_status_t spi_read_fifo(uint8_t *value)
{
    lt7680_status_t st;

    st = spi_wait(SPI_STATUS_RX_EMPTY, 0u);
    if (st != LT7680_OK) {
        return st;
    }
    return lt7680_read_reg(REG_SPIDR, value);
}

static lt7680_status_t spi_begin(void)
{
    lt7680_status_t st;
    uint8_t ccr;

    st = lt7680_read_reg(REG_CCR, &ccr);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_CCR, (uint8_t)(ccr | CCR_SPI_MASTER));
    if (st != LT7680_OK) {
        return st;
    }
    /* Defensively reset SFL_CTRL to the documented raw-read default (SF0,
     * text mode, 24-bit address) in case the power-on display boot loader
     * left the controller in DMA/font mode. The host sends 03h/9Fh itself,
     * so no SFL_CTRL command-code setup is required. */
    st = wr(REG_SFL_CTRL, SFL_CTRL_RAW_DEFAULT);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_SPI_DIV, SPI_DIVISOR_SAFE);
    if (st != LT7680_OK) {
        return st;
    }
    return wr(REG_SPIMCR2, SPI_CTRL_READ_ACTIVE);
}

static lt7680_status_t spi_push_and_drain(const uint8_t *tx, uint8_t count,
                                          uint8_t discard, uint8_t *data)
{
    uint8_t i;
    lt7680_status_t st;

    if (count == 0u || count > 16u || discard > count) {
        return LT7680_ERR_PARAM;
    }
    for (i = 0u; i < count; i++) {
        st = wr(REG_SPIDR, tx[i]);
        if (st != LT7680_OK) {
            return st;
        }
    }
    /* SPIMSR IDLE is interrupt-mask dependent on this controller. RX_EMPTY
     * supplies the required per-byte completion check while draining below. */
    st = spi_wait(SPI_STATUS_TX_EMPTY, SPI_STATUS_TX_EMPTY);
    if (st != LT7680_OK) {
        return st;
    }
    for (i = 0u; i < count; i++) {
        uint8_t value;
        st = spi_read_fifo(&value);
        if (st != LT7680_OK) {
            return st;
        }
        if (i >= discard) {
            data[i - discard] = value;
        }
    }
    return LT7680_OK;
}

lt7680_status_t lt7680_flash_read(uint32_t address, uint8_t *data,
                                  uint16_t length)
{
    uint8_t command[16];
    uint16_t remaining;
    lt7680_status_t st;

    if (data == 0 || length == 0u || address > 0x00FFFFFFu ||
        length > 0x01000000u - address) {
        return LT7680_ERR_PARAM;
    }
    st = spi_begin();
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
            st = spi_push_and_drain(command, (uint8_t)(4u + chunk), 4u, data);
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
                st = spi_push_and_drain(command, chunk, 0u, data);
                if (st != LT7680_OK) {
                    break;
                }
                data += chunk;
                remaining = (uint16_t)(remaining - chunk);
            }
        }
    }
    (void)wr(REG_SPIMCR2, SPI_CTRL_IDLE);
    return st;
}

lt7680_status_t lt7680_flash_read_jedec_id(uint8_t id[3])
{
    const uint8_t command[4] = {0x9Fu, 0u, 0u, 0u};
    lt7680_status_t st;

    if (id == 0) {
        return LT7680_ERR_PARAM;
    }
    st = spi_begin();
    if (st == LT7680_OK) {
        st = spi_push_and_drain(command, sizeof(command), 1u, id);
    }
    (void)wr(REG_SPIMCR2, SPI_CTRL_IDLE);
    return st;
}

/* Write a 13-bit value (coordinate or width) to a register pair, LSB first. */
static lt7680_status_t wr13(uint8_t reg, uint16_t val)
{
    lt7680_status_t st = wr(reg, (uint8_t)(val & 0xFFu));
    if (st != LT7680_OK) {
        return st;
    }
    return wr((uint8_t)(reg + 1u), (uint8_t)((val >> 8) & 0x1Fu));
}

/* Write a 32-bit little-endian value to 4 consecutive registers. */
static lt7680_status_t wr32le(uint8_t reg, uint32_t val)
{
    uint8_t i;
    for (i = 0; i < 4; i++) {
        lt7680_status_t st = wr((uint8_t)(reg + i),
                                (uint8_t)(val >> (8 * i)));
        if (st != LT7680_OK) {
            return st;
        }
    }
    return LT7680_OK;
}

/* Program one PLL.  FOUT = XI * (N / R) / OD  with XI = 10 MHz.
 * We pick R = 10 and OD = 1 (00b), so N = fout[MHz]. */
static lt7680_status_t pll_program(uint32_t fout_mhz, uint8_t c1reg,
                                   uint8_t c2reg)
{
    uint32_t n = fout_mhz;
    uint8_t r = 10u;
    uint8_t od = 0u;   /* 00b: divide by 1 */
    uint8_t c1;

    if (n < 2u) {
        n = 2u;
    }
    if (n > 511u) {
        n = 511u;
    }
    /* C1: bit[7:6] = OD, bit[5:1] = R, bit0 = N[8]. */
    c1 = (uint8_t)((od << 6) | (r << 1) | ((n >> 8) & 0x01u));
    {
        lt7680_status_t st = wr(c1reg, c1);
        if (st != LT7680_OK) {
            return st;
        }
        return wr(c2reg, (uint8_t)(n & 0xFFu));
    }
}

static lt7680_status_t wait_pll_ready(void)
{
    uint8_t ccr = 0;
    uint16_t i;
    for (i = 0; i < 2000u; i++) {
        lt7680_status_t st = lt7680_read_reg(REG_CCR, &ccr);
        if (st != LT7680_OK) {
            return st;
        }
        if ((ccr & 0x80u) != 0u) { /* bit7: PLL clock ready */
            return LT7680_OK;
        }
    }
    return LT7680_ERR_TIMEOUT;
}

static lt7680_status_t wait_sdram_ready(void)
{
    uint8_t status = 0;
    uint16_t i;
    for (i = 0; i < 1000u; i++) {
        lt7680_status_t st = lt7680_read_status(&status);
        if (st != LT7680_OK) {
            return st;
        }
        if ((status & LT7680_STATUS_DPRAM_READY) != 0u) {
            return LT7680_OK;
        }
    }
    return LT7680_ERR_TIMEOUT;
}

static lt7680_status_t set_active_window(const lt7680_rect_t *rect)
{
    lt7680_status_t st;

    st = wr13(REG_AWULX, rect->x);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(REG_AWULY, rect->y);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(REG_AWWTH, rect->w);
    if (st != LT7680_OK) {
        return st;
    }
    return wr13(REG_AWHT, rect->h);
}

static uint32_t panel_pclk(const lt7680_panel_t *p)
{
    uint32_t total_h = (uint32_t)p->width + p->hsw + p->hbp + p->hfp;
    uint32_t total_v = (uint32_t)p->height + p->vsw + p->vbp + p->vfp;
    return total_h * total_v * p->refresh_hz;
}

lt7680_status_t lt7680_gfx_init(const lt7680_panel_t *panel)
{
    lt7680_status_t st;

    if (panel == 0) {
        return LT7680_ERR_PARAM;
    }
    s_panel = *panel;

    /* --- 1. PLL: CCLK & MCLK at 100 MHz, PCLK from panel timing. --- */
    st = pll_program(PLL_CCLK_MHZ, REG_CPLLC1, REG_CPLLC2);

    st = pll_program(PLL_MCLK_MHZ, REG_MPLLC1, REG_MPLLC2);
    if (st != LT7680_OK) {
        return st;
    }
    st = pll_program((panel_pclk(panel) + 500000u) / 1000000u,
                     REG_PPLLC1, REG_PPLLC2);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_SRR, 0x80u);   /* bit7: apply new PLL settings */
    if (st != LT7680_OK) {
        return st;
    }
    st = wait_pll_ready();
    if (st != LT7680_OK) {
        return st;
    }

    /* --- 2. Display RAM (SDRAM) init for LT7680A-R (128 Mb). --- */
    st = wr(REG_SDRAR, 0x29u);  /* 4 banks, 4K rows, 512 cols (Table 19-6) */
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_SDRMD, 0x03u);  /* CAS latency 3 */
    if (st != LT7680_OK) {
        return st;
    }
    /* Refresh interval for MCLK = 100 MHz, rows = 4096: 0x061A. */
    st = wr(REG_SDRREF, 0x1Au);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_SDRREF + 1u, 0x06u);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_SDRCR, 0x01u);  /* bit0: start SDRAM init sequence */
    if (st != LT7680_OK) {
        return st;
    }
    st = wait_sdram_ready();
    if (st != LT7680_OK) {
        return st;
    }

    /* --- 3. Chip configuration & memory access control. --- */
    st = wr(REG_CCR, CCR_TFT_16BIT);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_MACR, 0x00u);  /* direct write, left->right, top->bottom */
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_ICR, 0x00u);   /* graphic mode, R/W destination = image buffer */
    if (st != LT7680_OK) {
        return st;
    }

    /* --- 4. Panel timing. --- */
    {
        uint8_t hdwr   = (uint8_t)((s_panel.width - 1u) >> 3); /* (HDWR+1)*8+F */
        uint8_t hdwftr = (uint8_t)((s_panel.width - 1u) & 0x07u);
        uint8_t hndr   = (uint8_t)((s_panel.hbp - 1u) >> 3);
        uint8_t hndftr = (uint8_t)((s_panel.hbp - 1u) & 0x07u);
        uint8_t hstr   = (uint8_t)((s_panel.hfp - 1u) >> 3);  /* (HSTR+1)*8 */
        uint8_t hpwr   = (uint8_t)((s_panel.hsw - 1u) >> 3);  /* (HPWR+1)*8 */

        st = wr(REG_HDWR, hdwr);
        if (st != LT7680_OK) {
            return st;
        }
        st = wr(REG_HDWFTR, hdwftr);
        if (st != LT7680_OK) {
            return st;
        }
        st = wr(REG_HNDR, hndr);
        if (st != LT7680_OK) {
            return st;
        }
        st = wr(REG_HNDFTR, hndftr);
        if (st != LT7680_OK) {
            return st;
        }
        st = wr(REG_HSTR, hstr);
        if (st != LT7680_OK) {
            return st;
        }
        st = wr(REG_HPWR, hpwr);
        if (st != LT7680_OK) {
            return st;
        }

        /* Vertical registers use value-1 encoding (VDHR+1, VNDR+1, ...). */
        st = wr13(REG_VDHR, (uint16_t)(s_panel.height - 1u));
        if (st != LT7680_OK) {
            return st;
        }
        st = wr13(REG_VNDR, (uint16_t)(s_panel.vbp - 1u));
        if (st != LT7680_OK) {
            return st;
        }
        st = wr13(REG_VSTR, (uint16_t)(s_panel.vfp - 1u));
        if (st != LT7680_OK) {
            return st;
        }
        st = wr13(REG_VPWR, (uint16_t)(s_panel.vsw - 1u));
        if (st != LT7680_OK) {
            return st;
        }
    }

    /* --- 5. Main window: full panel, start of display RAM. --- */
    st = wr(REG_MPWCTR, MPWCTR_MAIN_16BPP | MPWCTR_SYNC_MODE);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr32le(REG_MISA, 0x00u);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(REG_MIW, s_panel.width);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(REG_MWULX, 0u);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(REG_MWULY, 0u);
    if (st != LT7680_OK) {
        return st;
    }

    /* --- 6. Canvas: same area as the main window. --- */
    st = wr32le(REG_CVSSA, 0x00u);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(REG_CVSIMWTH, s_panel.width);
    if (st != LT7680_OK) {
        return st;
    }

    /* --- 7. Active window = whole panel, 16bpp block addressing. --- */
    {
        lt7680_rect_t full;
        full.x = 0;
        full.y = 0;
        full.w = s_panel.width;
        full.h = s_panel.height;
        st = set_active_window(&full);
        if (st != LT7680_OK) {
            return st;
        }
        st = wr(REG_AWCOLOR, AWCOLOR_BLOCK | AWCOLOR_16BPP);
        if (st != LT7680_OK) {
            return st;
        }
    }

    /* --- 8. Panel scan / sync polarity. --- */
    {
        uint8_t pcsr = 0u;
        if (s_panel.hsync_active_high) {
            pcsr |= 0x80u;
        }
        if (s_panel.vsync_active_high) {
            pcsr |= 0x40u;
        }
        st = wr(REG_PCSR, pcsr);
        if (st != LT7680_OK) {
            return st;
        }
    }

    /* --- 9. Display on. --- */
    st = wr(REG_DPCR, DPCR_DISPLAY_ON |
                          (s_panel.pclk_invert ? DPCR_PCLK_INVERT : 0u) |
                          (uint8_t)(s_panel.rgb_order & 0x07u));
    if (st != LT7680_OK) {
        return st;
    }

    return LT7680_OK;
}

lt7680_status_t lt7680_gfx_clear(uint16_t rgb565)
{
    lt7680_rect_t full;
    full.x = 0;
    full.y = 0;
    full.w = s_panel.width;
    full.h = s_panel.height;
    return lt7680_gfx_fill_rect(&full, rgb565);
}

/* RGB565 -> LT7680 16bpp foreground color. LT7680 "65K colors" layout:
 * R = FGCR[7:3], G = FGCG[7:2], B = FGCB[7:3] (datasheet V4.2). */
static lt7680_status_t set_fg_color16(uint16_t rgb565)
{
    lt7680_status_t st;
    uint8_t r = (uint8_t)((rgb565 >> 11) & 0x1Fu);
    uint8_t g = (uint8_t)((rgb565 >> 5)  & 0x3Fu);
    uint8_t b = (uint8_t)(rgb565          & 0x1Fu);

    st = wr(REG_FGCR, (uint8_t)(r << 3));
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_FGCG, (uint8_t)(g << 2));
    if (st != LT7680_OK) {
        return st;
    }
    return wr(REG_FGCB, (uint8_t)(b << 3));
}

/* Rectangle fill via the geometry engine (LT768x DS V4.2 + Levetop
 * LT768_Lib / RAiO Ra8876_Lite): DCR1 REG[76h] = 0xE0 -> bit7 start,
 * bit6 fill, bit[5:4]=10b rectangle.  DCR0 REG[67h] only handles
 * line/triangle.  Start/end points are exclusive: end = (x+w, y+h). */
lt7680_status_t lt7680_gfx_fill_rect(const lt7680_rect_t *rect, uint16_t rgb565)
{
    lt7680_status_t st;
    uint32_t end_x;
    uint32_t end_y;

    if (rect == 0) {
        return LT7680_ERR_PARAM;
    }

    st = set_fg_color16(rgb565);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr32le(REG_GE_SPT, ((uint32_t)rect->x & 0x1FFFu) |
                            (((uint32_t)rect->y & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }

    end_x = (uint32_t)rect->x + (uint32_t)rect->w;
    end_y = (uint32_t)rect->y + (uint32_t)rect->h;
    st = wr32le(REG_GE_EPT, (end_x & 0x1FFFu) |
                            ((end_y & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }

    return wr(REG_DCR1, 0xE0u);
}

/* Direct pixel write.  The active window must be set first.  Point the data
 * port at Display RAM (REG[04h], memory-write step 4) before pushing the
 * 16bpp pixel LSB first; otherwise the bytes land in the last-addressed
 * register (CURV) instead of memory. */
lt7680_status_t lt7680_gfx_set_pixel(uint16_t x, uint16_t y, uint16_t rgb565)
{
    lt7680_status_t st;
    uint8_t pixel[2];

    st = wr13(REG_CURH, x);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(REG_CURV, y);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_select_reg(REG_MRWDP);
    if (st != LT7680_OK) {
        return st;
    }
    pixel[0] = (uint8_t)(rgb565 & 0xFFu);
    pixel[1] = (uint8_t)(rgb565 >> 8);
    return lt7680_write_data(pixel, 2u);
}

lt7680_status_t lt7680_gfx_peek_pixel(uint16_t x, uint16_t y, uint16_t *rgb565)
{
    lt7680_status_t st;
    uint8_t lo = 0u;
    uint8_t hi = 0u;

    if (rgb565 == 0) {
        return LT7680_ERR_PARAM;
    }
    st = wr13(REG_CURH, x);
    if (st != LT7680_OK) {
        return st;
    }
    st = wr13(REG_CURV, y);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_select_reg(REG_MRWDP);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_read_reg(REG_MRWDP, &lo);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_read_reg(REG_MRWDP, &hi);
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
            bitmap = font_text_bitmap('?');
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

/* Wait for the geometry engine to finish the current draw. The 2D engine
 * sets status bit 0x08 (CORE_BUSY) while rasterizing; the Levetop AP-Note
 * polls the same bit after every draw start. */
static lt7680_status_t wait_2d_idle(void)
{
    uint8_t status = 0;
    uint16_t i;
    for (i = 0; i < 1000u; i++) {
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

/* Draw a line through the geometry engine (Levetop AP-Note):
 * - REG[68h..6Bh] = start point (13-bit x, 13-bit y, LSB first)
 * - REG[6Ch..6Fh] = end point
 * - DCR0 REG[67h] = 0x80: bit7 start, no fill, line command (bits[4:1]=0). */
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
    st = wr32le(REG_GE_SPT, ((uint32_t)x0 & 0x1FFFu) |
                            (((uint32_t)y0 & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    st = wr32le(REG_GE_EPT, ((uint32_t)x1 & 0x1FFFu) |
                            (((uint32_t)y1 & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_DCR0, DCR0_DRAW_EN);
    if (st != LT7680_OK) {
        return st;
    }
    return wait_2d_idle();
}

/* Draw connected line segments; xy holds n_points pairs (x, y). */
lt7680_status_t lt7680_gfx_draw_polyline(const int16_t *xy, uint16_t n_points,
                                         uint16_t rgb565)
{
    uint16_t i;

    if (xy == 0 || n_points < 2u) {
        return LT7680_ERR_PARAM;
    }
    for (i = 0; i + 1u < n_points; i++) {
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

/* Draw a circle through the geometry engine (Levetop AP-Note):
 * - REG[77h..7Ah] = radius (major lo/hi then minor lo/hi, both = r)
 * - REG[7Bh..7Eh] = center point
 * - DCR1 REG[76h] = 0x80: bit7 start, non-fill circle/ellipse. */
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
    st = wr32le(REG_GE_RAD, ((uint32_t)r & 0x1FFFu) |
                            (((uint32_t)r & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    st = wr32le(REG_GE_CPT, ((uint32_t)xc & 0x1FFFu) |
                            (((uint32_t)yc & 0x1FFFu) << 16));
    if (st != LT7680_OK) {
        return st;
    }
    st = wr(REG_DCR1, DCR1_DRAW_EN);
    if (st != LT7680_OK) {
        return st;
    }
    return wait_2d_idle();
}
