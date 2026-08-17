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

typedef struct {
    uint8_t attempted;
    uint8_t requested;
    uint8_t readback;
    lt7680_status_t write_status;
    lt7680_status_t read_status;
} lt7680_flash_b7_probe_t;

/* Read-only diagnostic: records whether SPIMSR TX_FULL (BA bit6) was observed
 * before each SPIDR write during a flash FIFO transaction. Initialized to
 * zero/not-attempted; never modified by normal read operations. */
typedef struct {
    uint8_t attempted;   /* 1 if any SPIDR push loop was entered */
    uint8_t full_count;  /* number of SPIDR writes where TX_FULL was seen */
    uint8_t status_err;  /* 1 if SPIMSR read failed at least once */
    uint8_t last_status; /* last successfully read SPIMSR value */
} lt7680_flash_fifo_probe_t;

/* Read-only diagnostic: captures the raw 4-byte SPIDR read from a JEDEC ID
 * transaction (0x9F). The first byte is the turnaround byte (discarded by
 * lt7680_flash_read_jedec_id), followed by the three manufacturer/device
 * ID bytes. Useful for distinguishing U5 MISO stuck-low from LT7680 SPIDR
 * readback anomalies. */
typedef struct {
    uint8_t attempted;   /* 1 if JEDEC transaction was attempted */
    uint8_t raw[4];      /* raw SPIDR reads: [0]=turnaround, [1..3]=ID */
    lt7680_status_t status; /* status of the JEDEC transaction */
} lt7680_flash_jedec_probe_t;

/* Read-only diagnostic for the existing RIF header read at address zero. */
typedef struct {
    uint8_t attempted;
    uint8_t raw[16];
    lt7680_status_t status;
} lt7680_flash_header_probe_t;

lt7680_status_t lt7680_gfx_init(const lt7680_panel_t *panel);
lt7680_status_t lt7680_gfx_clear(uint16_t rgb565);
lt7680_status_t lt7680_gfx_fill_rect(const lt7680_rect_t *rect, uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_rect(const lt7680_rect_t *rect, uint16_t rgb565);
lt7680_status_t lt7680_gfx_set_pixel(uint16_t x, uint16_t y, uint16_t rgb565);
lt7680_status_t lt7680_gfx_peek_pixel(uint16_t x, uint16_t y, uint16_t *rgb565);
lt7680_status_t lt7680_gfx_draw_line(int16_t x0, int16_t y0, int16_t x1,
                                     int16_t y1, uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_polyline(const int16_t *xy, uint16_t n_points,
                                         uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_circle(int16_t xc, int16_t yc, int16_t r,
                                       uint16_t rgb565);
lt7680_status_t lt7680_gfx_draw_text(uint16_t x, uint16_t y, const char *text,
                                      uint16_t fg, uint16_t bg);
lt7680_status_t lt7680_flash_read(uint32_t address, uint8_t *data,
                                  uint16_t length);
lt7680_status_t lt7680_flash_read_jedec_id(uint8_t id[3]);
void lt7680_flash_get_b7_probe(lt7680_flash_b7_probe_t *probe);
void lt7680_flash_get_fifo_probe(lt7680_flash_fifo_probe_t *probe);
void lt7680_flash_get_jedec_probe(lt7680_flash_jedec_probe_t *probe);
void lt7680_flash_get_header_probe(lt7680_flash_header_probe_t *probe);
