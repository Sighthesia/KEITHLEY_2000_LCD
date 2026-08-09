#pragma once

#include <stdint.h>

#include "lt7680_bus.h"

/* Panel timing parameters for PLL and display setup. Names follow
 * LT768x_DS V4.2 section 4 (clock setup) and the AP-Note lib header. */
typedef struct {
    uint16_t width;   /* horizontal active pixels, e.g. 480 */
    uint16_t height;  /* vertical active lines, e.g. 272 */
    uint8_t bpp;      /* 8, 16 or 24 (main window color depth) */
    uint8_t hsw;      /* HSYNC pulse width (pixels) */
    uint16_t hbp;     /* horizontal back porch (pixels) */
    uint16_t hfp;     /* horizontal front porch (pixels) */
    uint8_t vsw;      /* VSYNC pulse width (lines) */
    uint16_t vbp;     /* vertical back porch (lines) */
    uint16_t vfp;     /* vertical front porch (lines) */
    uint32_t refresh_hz;  /* frame rate used for PCLK, e.g. 60 */
    uint8_t hsync_active_high;  /* 1 = HSYNC high active, 0 = low */
    uint8_t vsync_active_high;  /* 1 = VSYNC high active, 0 = low */
    uint8_t pclk_invert;        /* 1 = panel fetches on PCLK falling edge */
    uint8_t rgb_order;          /* REG[12h] bits[2:0]: RGB/RBG/... */
    uint32_t dram_start;        /* main window start address in Display RAM,
                                   byte aligned, e.g. 0x000000 */
} lt7680_panel_t;

/* Rectangle in panel coordinates, exclusive end point (x+w, y+h). */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} lt7680_rect_t;

lt7680_status_t lt7680_gfx_init(const lt7680_panel_t *panel);
lt7680_status_t lt7680_gfx_clear(uint16_t rgb565);
lt7680_status_t lt7680_gfx_fill_rect(const lt7680_rect_t *rect, uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_rect(const lt7680_rect_t *rect, uint16_t rgb565);
lt7680_status_t lt7680_gfx_set_pixel(uint16_t x, uint16_t y, uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_line(int16_t x0, int16_t y0, int16_t x1,
                                     int16_t y1, uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_polyline(const int16_t *xy, uint16_t n_points,
                                         uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_circle(int16_t xc, int16_t yc, int16_t r,
                                       uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_text(uint16_t x, uint16_t y, const char *text,
                                     uint16_t fg, uint16_t bg);
