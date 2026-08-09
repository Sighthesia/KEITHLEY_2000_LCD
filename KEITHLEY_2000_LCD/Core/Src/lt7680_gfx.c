#include "lt7680_gfx.h"

static lt7680_panel_t s_panel;

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
    /* REG[02h]: V16 RMW = clear bit7, set bit6 (write 1), then clear
     * bit2+bit1 (write 2, mask 0xF9): clear mask 0x86, set 0x40. */
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
    return configure_panel();
}

lt7680_status_t lt7680_gfx_show_color_bars(void)
{
    /* V16 display-on adds bit6 (0x40) on top of the init value (bit3 set,
     * bit7/bit4/bits0-2 clear).  Color-bar test pattern is bit5 (0x20). */
    return rmw_reg(LT7680_REG_DISPLAY_CTRL, 0x68u, 0x97u);
}

lt7680_status_t lt7680_gfx_clear(uint16_t rgb565)
{
    (void)rgb565;
    return LT7680_ERR_PARAM;
}

lt7680_status_t lt7680_gfx_fill_rect(const lt7680_rect_t *rect, uint16_t rgb565)
{
    (void)rect;
    (void)rgb565;
    return LT7680_ERR_PARAM;
}

lt7680_status_t lt7680_gfx_draw_rect(const lt7680_rect_t *rect, uint16_t rgb565)
{
    (void)rect;
    (void)rgb565;
    return LT7680_ERR_PARAM;
}

lt7680_status_t lt7680_gfx_draw_text(uint16_t x, uint16_t y, const char *text,
                                      uint16_t fg, uint16_t bg)
{
    (void)x;
    (void)y;
    (void)text;
    (void)fg;
    (void)bg;
    return LT7680_ERR_PARAM;
}
