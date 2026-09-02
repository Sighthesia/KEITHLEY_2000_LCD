#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} ui_rect_t;

uint16_t ui_measure_text(const char *text);
bool ui_rect_overlap(const ui_rect_t *a, const ui_rect_t *b);
ui_rect_t ui_rect_from_text(uint16_t x, uint16_t y, const char *text, uint16_t h);

/* Second row layout: left green function + right metadata.
 * Inputs are frame strings, outputs are x positions for each field and squeezed widths.
 * Returns true if layout fits without truncation, false if squeezed.
 */
bool ui_layout_second_row(const char *function, const char *impedance,
                          const char *range, const char *rate,
                          uint16_t *out_function_w,
                          uint16_t *out_zin_x, uint16_t *out_imp_x,
                          uint16_t *out_range_label_x, uint16_t *out_range_x,
                          uint16_t *out_rate_label_x, uint16_t *out_rate_x,
                          uint16_t *out_lamps_x);

bool ui_layout_top_bar(const char *brand, const char *active,
                       const char *temperature, const char *uptime,
                       uint16_t *out_brand_x, uint16_t *out_active_x,
                       uint16_t *out_temp_x, uint16_t *out_uptime_x);
