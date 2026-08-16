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
/* Select one of the two complete 320x960 RGB565 canvas pages for GE writes. */
lt7680_status_t lt7680_gfx_select_canvas_page(uint8_t page);
/* Atomically make a completed canvas page the visible main image. */
lt7680_status_t lt7680_gfx_present_page(uint8_t page);
/* Clone one complete RGB565 canvas page with the verified BTE copy ROP. */
lt7680_status_t lt7680_gfx_copy_page(uint8_t source_page, uint8_t target_page);
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
lt7680_status_t lt7680_gfx_set_pixel(uint16_t x, uint16_t y, uint16_t rgb565);
lt7680_status_t lt7680_gfx_peek_pixel(uint16_t x, uint16_t y, uint16_t *rgb565);
lt7680_status_t lt7680_flash_read(uint32_t address, uint8_t *data,
                                  uint16_t length);
lt7680_status_t lt7680_flash_read_jedec_id(uint8_t id[3]);
