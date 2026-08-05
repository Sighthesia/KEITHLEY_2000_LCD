#include "lt7680_gfx.h"

static lt7680_panel_t s_panel;

/* Placeholder register addresses - resolve from LT7680A-R datasheet V4.2. */
#define LT7680_REG_MAIN_WIN_START_X 0x0300u
#define LT7680_REG_MAIN_WIN_END_X 0x0302u
#define LT7680_REG_MAIN_WIN_START_Y 0x0304u
#define LT7680_REG_MAIN_WIN_END_Y 0x0306u
#define LT7680_REG_MAIN_WIN_BASE_ADDR 0x0308u
#define LT7680_REG_MEMORY_WRITE_X 0x030Cu
#define LT7680_REG_MEMORY_WRITE_Y 0x030Eu
#define LT7680_REG_MEMORY_WRITE_XY 0x0310u

lt7680_status_t lt7680_gfx_init(const lt7680_panel_t *panel)
{
    if (panel == 0) {
        return LT7680_ERR_PARAM;
    }
    s_panel = *panel;
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

lt7680_status_t lt7680_gfx_fill_rect(const lt7680_rect_t *rect, uint16_t rgb565)
{
    uint8_t color_lo;
    uint8_t color_hi;
    uint32_t pixels;
    lt7680_status_t st;

    if (rect == 0) {
        return LT7680_ERR_PARAM;
    }
    if (rect->w == 0 || rect->h == 0) {
        return LT7680_OK;
    }

    st = lt7680_write_reg(LT7680_REG_MEMORY_WRITE_X, rect->x);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_write_reg(LT7680_REG_MEMORY_WRITE_Y, rect->y);
    if (st != LT7680_OK) {
        return st;
    }

    /* 16bpp: two bytes per pixel, RGB565. */
    pixels = (uint32_t)rect->w * rect->h;
    color_lo = (uint8_t)(rgb565 & 0xFFu);
    color_hi = (uint8_t)((rgb565 >> 8) & 0xFFu);

    {
        uint32_t i;
        for (i = 0; i < pixels; i++) {
            lt7680_status_t st2 = lt7680_write_data(&color_lo, 1);
            if (st2 != LT7680_OK) {
                return st2;
            }
            st2 = lt7680_write_data(&color_hi, 1);
            if (st2 != LT7680_OK) {
                return st2;
            }
        }
    }

    return LT7680_OK;
}

lt7680_status_t lt7680_gfx_draw_rect(const lt7680_rect_t *rect, uint16_t rgb565)
{
    lt7680_rect_t top;
    lt7680_rect_t bottom;
    lt7680_rect_t left;
    lt7680_rect_t right;
    lt7680_status_t st;

    if (rect == 0) {
        return LT7680_ERR_PARAM;
    }
    if (rect->w < 2 || rect->h < 2) {
        return lt7680_gfx_fill_rect(rect, rgb565);
    }

    top.x = rect->x;
    top.y = rect->y;
    top.w = rect->w;
    top.h = 1;

    bottom.x = rect->x;
    bottom.y = rect->y + rect->h - 1;
    bottom.w = rect->w;
    bottom.h = 1;

    left.x = rect->x;
    left.y = rect->y;
    left.w = 1;
    left.h = rect->h;

    right.x = rect->x + rect->w - 1;
    right.y = rect->y;
    right.w = 1;
    right.h = rect->h;

    st = lt7680_gfx_fill_rect(&top, rgb565);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_gfx_fill_rect(&bottom, rgb565);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_gfx_fill_rect(&left, rgb565);
    if (st != LT7680_OK) {
        return st;
    }
    st = lt7680_gfx_fill_rect(&right, rgb565);
    if (st != LT7680_OK) {
        return st;
    }

    return LT7680_OK;
}

lt7680_status_t lt7680_gfx_draw_text(uint16_t x, uint16_t y, const char *text,
                                     uint16_t fg, uint16_t bg)
{
    if (text == 0) {
        return LT7680_ERR_PARAM;
    }
    /* Text engine requires CGROM / UCG setup; add after basic bus bring-up. */
    (void)x;
    (void)y;
    (void)fg;
    (void)bg;
    return LT7680_OK;
}
