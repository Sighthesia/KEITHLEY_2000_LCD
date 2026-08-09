#pragma once

#include <stdint.h>

#include "lt7680_bus.h"

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
} lt7680_panel_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} lt7680_rect_t;

lt7680_status_t lt7680_gfx_init(const lt7680_panel_t *panel);
lt7680_status_t lt7680_gfx_show_color_bars(void);
lt7680_status_t lt7680_gfx_clear(uint16_t rgb565);
lt7680_status_t lt7680_gfx_fill_rect(const lt7680_rect_t *rect, uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_rect(const lt7680_rect_t *rect, uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_line(int16_t x0, int16_t y0, int16_t x1,
                                     int16_t y1, uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_polyline(const int16_t *xy, uint16_t n_points,
                                         uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_circle(int16_t xc, int16_t yc, int16_t r,
                                       uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_text(uint16_t x, uint16_t y, const char *text,
                                     uint16_t fg, uint16_t bg);
