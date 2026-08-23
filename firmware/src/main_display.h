#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font_digits.h"
#include "font_half.h"
#include "font_text.h"
#include "trend_buffer.h"
#include "ui_model.h"

#define MAIN_DISPLAY_UI_WIDTH 960u
#define MAIN_DISPLAY_UI_HEIGHT 320u

/* The deleted function/parameters/graph-header rows free the full y24..192
 * band for the reading. The value and unit are drawn at digit size
 * left-aligned; the half-height DC/AC suffix shares the digit font's
 * typographic baseline, so its ink bottom lands exactly on the reading's ink
 * bottom (cell-bottom alignment would drop it by the two fonts' different
 * baseline insets). The right info panel is a 4-row rectangle of Excel-style
 * name/value cells (Zin / Range / Rate / Status) inside the same band. */
#define MAIN_DISPLAY_STATUS_Y 0u
#define MAIN_DISPLAY_STATUS_H 24u
#define MAIN_DISPLAY_READING_Y 24u
#define MAIN_DISPLAY_READING_H 168u
#define MAIN_DISPLAY_TREND_Y 192u
#define MAIN_DISPLAY_TREND_H 128u

#define MAIN_DISPLAY_READING_X 12u
#define MAIN_DISPLAY_INFO_RIGHT 940u
#define MAIN_DISPLAY_INFO_X 720u
#define MAIN_DISPLAY_INFO_W \
    (MAIN_DISPLAY_INFO_RIGHT - MAIN_DISPLAY_INFO_X)
#define MAIN_DISPLAY_INFO_NAME_W 72u
#define MAIN_DISPLAY_INFO_VALUE_W \
    (MAIN_DISPLAY_INFO_W - MAIN_DISPLAY_INFO_NAME_W)
#define MAIN_DISPLAY_INFO_ROW_H 32u
#define MAIN_DISPLAY_READING_VALUE_W \
    (MAIN_DISPLAY_INFO_RIGHT - MAIN_DISPLAY_INFO_W - MAIN_DISPLAY_READING_X)
#define MAIN_DISPLAY_MAX_SLOTS (MAIN_DISPLAY_READING_VALUE_W / FONT_DIGIT_WIDTH)
/* Bottom-align the digits with the info panel's bottom edge so the
 * reading and the 4-row cell block share one visual baseline. */
#define MAIN_DISPLAY_READING_VALUE_Y \
    (MAIN_DISPLAY_READING_Y + MAIN_DISPLAY_READING_H - FONT_DIGIT_HEIGHT)
#define MAIN_DISPLAY_DCAC_Y \
    (MAIN_DISPLAY_READING_VALUE_Y + FONT_DIGIT_BASELINE - FONT_HALF_BASELINE)
#define MAIN_DISPLAY_INFO_ZIN_Y 40u
#define MAIN_DISPLAY_INFO_RANGE_Y 72u
#define MAIN_DISPLAY_INFO_RATE_Y 104u
#define MAIN_DISPLAY_INFO_STATUS_Y 136u
#define MAIN_DISPLAY_STATUS_LABEL_GAP 12u
#define MAIN_DISPLAY_STATUS_LABEL_MAX 8u
#define MAIN_DISPLAY_FUNCTION_MAX 20u
#define MAIN_DISPLAY_META_MAX 32u
#define MAIN_DISPLAY_AXIS_LABEL_MAX 16u
#define MAIN_DISPLAY_Y_LABEL_COUNT 4u
#define MAIN_DISPLAY_X_LABEL_COUNT 5u

#define MAIN_DISPLAY_COLOR_BG 0x0000u
#define MAIN_DISPLAY_COLOR_BAR 0x18C3u
#define MAIN_DISPLAY_COLOR_BAR_ALT 0x2945u
#define MAIN_DISPLAY_COLOR_GREEN 0x07E6u
#define MAIN_DISPLAY_COLOR_GREEN_DIM 0x0323u
#define MAIN_DISPLAY_COLOR_CYAN 0x07FFu
#define MAIN_DISPLAY_COLOR_WHITE 0xFFFFu
#define MAIN_DISPLAY_COLOR_MUTED 0x632Cu
#define MAIN_DISPLAY_COLOR_GRID 0x3186u
#define MAIN_DISPLAY_COLOR_RED 0xF800u

#define MAIN_DISPLAY_PLOT_X 96u
#define MAIN_DISPLAY_PLOT_Y 196u
#define MAIN_DISPLAY_PLOT_W 840u
#define MAIN_DISPLAY_PLOT_H 92u
#define MAIN_DISPLAY_X_LABEL_Y 296u
#define MAIN_DISPLAY_CHART_PANEL_Y MAIN_DISPLAY_TREND_Y
#define MAIN_DISPLAY_CHART_PANEL_H MAIN_DISPLAY_TREND_H
#define MAIN_DISPLAY_PLOT_DIVIDER_H 4u
#define MAIN_DISPLAY_PLOT_DIVIDER_Y MAIN_DISPLAY_TREND_Y
#define MAIN_DISPLAY_PLOT_BG_Y MAIN_DISPLAY_PLOT_Y
#define MAIN_DISPLAY_PLOT_BG_H MAIN_DISPLAY_PLOT_H

/* Axis cells use the same deep-grey name-cell treatment as the info panel. */
#define MAIN_DISPLAY_Y_AXIS_X 0u
#define MAIN_DISPLAY_Y_AXIS_W MAIN_DISPLAY_PLOT_X
#define MAIN_DISPLAY_Y_AXIS_Y MAIN_DISPLAY_TREND_Y
#define MAIN_DISPLAY_Y_AXIS_H MAIN_DISPLAY_TREND_H
#define MAIN_DISPLAY_X_AXIS_X MAIN_DISPLAY_PLOT_X
#define MAIN_DISPLAY_X_AXIS_W MAIN_DISPLAY_PLOT_W
#define MAIN_DISPLAY_X_AXIS_Y MAIN_DISPLAY_X_LABEL_Y
#define MAIN_DISPLAY_X_AXIS_H 24u

typedef struct {
    uint16_t start_x;
    uint16_t end_x;
    uint8_t valid;
} main_display_layout_t;

typedef struct {
    char value[UI_MODEL_MAX_FIELD];
    char unit[UI_MODEL_MAX_UNIT];
    char unit_suffix[4];
    uint8_t value_len;
    uint8_t unit_len;
    uint8_t special;
    bool no_data;
    uint16_t value_color;
    uint16_t start_x;
    uint16_t end_x;
    uint16_t reading_y;
    char function[MAIN_DISPLAY_FUNCTION_MAX];
    char impedance[MAIN_DISPLAY_META_MAX];
    char range[MAIN_DISPLAY_META_MAX];
    char filter[MAIN_DISPLAY_META_MAX];
    char rate[MAIN_DISPLAY_META_MAX];
    char gpib[MAIN_DISPLAY_META_MAX];
    char buffer[MAIN_DISPLAY_META_MAX];
    uint8_t status_count;
    char status_text[STATUS_BAR_CORE_COUNT][MAIN_DISPLAY_STATUS_LABEL_MAX];
    bool status_active[STATUS_BAR_CORE_COUNT];
    char y_labels[MAIN_DISPLAY_Y_LABEL_COUNT][MAIN_DISPLAY_AXIS_LABEL_MAX];
    char x_labels[MAIN_DISPLAY_X_LABEL_COUNT][MAIN_DISPLAY_AXIS_LABEL_MAX];
    bool trend_has_data;
    float trend_minimum;
    float trend_maximum;
} main_display_frame_t;

main_display_layout_t main_display_layout_value(uint8_t value_len,
                                                 uint16_t start_x,
                                                 uint8_t slots);
uint16_t main_display_cursor_x(const main_display_layout_t *layout,
                               uint16_t cursor_pos);
uint16_t main_display_special_color(uint8_t special);
const char *main_display_rate_text(ui_rate_t rate);
const char *main_display_function_text(ui_function_t function);
void main_display_format(const ui_model_t *model, main_display_frame_t *frame);
void main_display_format_trend(const trend_buffer_t *trend, uint32_t now_ms,
                               const char *unit, main_display_frame_t *frame);
