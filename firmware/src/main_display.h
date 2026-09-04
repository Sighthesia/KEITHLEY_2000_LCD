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

/* Layout v2.3: the first two rows are a compact instrument header. */
#define MAIN_DISPLAY_STATUS_Y 0u
#define MAIN_DISPLAY_STATUS_H 24u
#define MAIN_DISPLAY_INFO_BAR_Y 24u
#define MAIN_DISPLAY_INFO_BAR_H 26u
#define MAIN_DISPLAY_INFO_BAR_X 12u
#define MAIN_DISPLAY_INFO_RIGHT 940u
#define MAIN_DISPLAY_INFO_BAR_W \
    (MAIN_DISPLAY_INFO_RIGHT - MAIN_DISPLAY_INFO_BAR_X)
#define MAIN_DISPLAY_READING_Y 50u
#define MAIN_DISPLAY_READING_H 142u
#define MAIN_DISPLAY_TREND_Y 192u
#define MAIN_DISPLAY_TREND_H 128u

#define MAIN_DISPLAY_READING_X 12u
/* Compat: old right-panel geometry (now unused, kept for verify) */
#define MAIN_DISPLAY_INFO_X 760u
#define MAIN_DISPLAY_INFO_W \
    (MAIN_DISPLAY_INFO_RIGHT - MAIN_DISPLAY_INFO_X)
#define MAIN_DISPLAY_INFO_NAME_W 72u
#define MAIN_DISPLAY_INFO_VALUE_W \
    (MAIN_DISPLAY_INFO_W - MAIN_DISPLAY_INFO_NAME_W)
#define MAIN_DISPLAY_INFO_ROW_H 32u
#define MAIN_DISPLAY_READING_VALUE_W \
    (MAIN_DISPLAY_INFO_BAR_W - FONT_DIGIT_WIDTH)
#define MAIN_DISPLAY_MAX_SLOTS (MAIN_DISPLAY_READING_VALUE_W / FONT_DIGIT_WIDTH)
/* Bottom-align the 128-pixel tiles to the reading-band bottom. */
#define MAIN_DISPLAY_READING_VALUE_Y \
    (MAIN_DISPLAY_READING_Y + MAIN_DISPLAY_READING_H - FONT_DIGIT_HEIGHT)
#define MAIN_DISPLAY_DCAC_Y \
    (MAIN_DISPLAY_READING_VALUE_Y + FONT_DIGIT_BASELINE - FONT_HALF_BASELINE)
#define MAIN_DISPLAY_INFO_ZIN_Y MAIN_DISPLAY_INFO_BAR_Y
#define MAIN_DISPLAY_INFO_RANGE_Y MAIN_DISPLAY_INFO_BAR_Y
#define MAIN_DISPLAY_INFO_RATE_Y MAIN_DISPLAY_INFO_BAR_Y
#define MAIN_DISPLAY_INFO_STATUS_Y MAIN_DISPLAY_INFO_BAR_Y
#define MAIN_DISPLAY_STATUS_LABEL_GAP 12u
#define MAIN_DISPLAY_STATUS_LABEL_MAX 8u
#define MAIN_DISPLAY_FUNCTION_MAX 20u
#define MAIN_DISPLAY_META_MAX 32u
#define MAIN_DISPLAY_AXIS_LABEL_MAX 16u
#define MAIN_DISPLAY_Y_LABEL_COUNT 3u
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
#define MAIN_DISPLAY_COLOR_YELLOW 0xFFE0u
#define MAIN_DISPLAY_COLOR_BLACK 0x0000u
/* Header rows redesign (ADR-0004): badge + divider are green, black badge
 * text; light separators reuse the grid color (same family as the stats
 * cells' hairlines). */
#define MAIN_DISPLAY_COLOR_BADGE_BG MAIN_DISPLAY_COLOR_GREEN
#define MAIN_DISPLAY_COLOR_BADGE_TEXT MAIN_DISPLAY_COLOR_BLACK
#define MAIN_DISPLAY_COLOR_BRAND_BG MAIN_DISPLAY_COLOR_RED
#define MAIN_DISPLAY_COLOR_BRAND_TEXT MAIN_DISPLAY_COLOR_WHITE
/* Gap between the red KEITHLEY badge and the plain "2000" tail. */
#define MAIN_DISPLAY_BRAND_TAIL_GAP 6u
#define MAIN_DISPLAY_COLOR_DIVIDER MAIN_DISPLAY_COLOR_GREEN
#define MAIN_DISPLAY_COLOR_SEP MAIN_DISPLAY_COLOR_GRID
#define MAIN_DISPLAY_BADGE_PAD_X 10u
#define MAIN_DISPLAY_BADGE_TEXT_GAP 8u
#define MAIN_DISPLAY_YELLOW_LINE_H 2u
#define MAIN_DISPLAY_BADGE_X 0u
/* Row 1: 1px half-height separator after the brand + left-aligned lamps. */
#define MAIN_DISPLAY_ROW1_SEP_GAP 10u
#define MAIN_DISPLAY_ROW1_SEP_W 1u
#define MAIN_DISPLAY_ROW1_SEP_H 12u
#define MAIN_DISPLAY_ROW1_INFO_GAP 10u
/* Row 2: stats-style name/value cells (BAR/BAR_ALT) + trigger block. */
#define MAIN_DISPLAY_ROW2_CELL_PAD_X 4u
#define MAIN_DISPLAY_ROW2_BLOCK_GAP 8u
#define MAIN_DISPLAY_ROW2_TRIG_GAP 10u
#define MAIN_DISPLAY_TRIG_MARGIN_R 12u
#define MAIN_DISPLAY_TRIG_DOT_SIZE 8u
#define MAIN_DISPLAY_TRIG_DOT_GAP 8u

/* Bottom band composition (ADR-0007): 26px header (Trend + range + stats),
 * plot with a 96px Y gutter, 24px taskbar with elapsed-time labels.
 * Legacy-only geometry below (DIVIDER/BG/AXIS/STATS/X_LABEL) is kept for the
 * dormant non-baseline renderer and verify compat. */
#define MAIN_DISPLAY_TREND_HEADER_Y MAIN_DISPLAY_TREND_Y
#define MAIN_DISPLAY_TREND_HEADER_H 26u
#define MAIN_DISPLAY_TREND_TASKBAR_H 24u
#define MAIN_DISPLAY_TREND_TASKBAR_Y \
    (MAIN_DISPLAY_TREND_Y + MAIN_DISPLAY_TREND_H - MAIN_DISPLAY_TREND_TASKBAR_H)
#define MAIN_DISPLAY_TREND_GUTTER_W 96u
/* "Trend" badge: 5 chars * 12px + 2 * 10px pad, function-badge language. */
#define MAIN_DISPLAY_TREND_BADGE_W 80u
/* Header stat slots: fixed left/mid/right positions, "NAME " + up to 8 bare
 * digits (no unit). Fixed slots kill the auto-squeeze jitter. */
#define MAIN_DISPLAY_TREND_STAT_X0 120u
#define MAIN_DISPLAY_TREND_STAT_PITCH 300u
#define MAIN_DISPLAY_TREND_STAT_CHARS 8u
#define MAIN_DISPLAY_PLOT_X MAIN_DISPLAY_TREND_GUTTER_W
#define MAIN_DISPLAY_PLOT_Y \
    (MAIN_DISPLAY_TREND_HEADER_Y + MAIN_DISPLAY_TREND_HEADER_H)
#define MAIN_DISPLAY_PLOT_W \
    (MAIN_DISPLAY_UI_WIDTH - MAIN_DISPLAY_TREND_GUTTER_W)
#define MAIN_DISPLAY_PLOT_H \
    (MAIN_DISPLAY_TREND_TASKBAR_Y - MAIN_DISPLAY_PLOT_Y)
#define MAIN_DISPLAY_TREND_GRID_COUNT 5u
#define MAIN_DISPLAY_TREND_STATS_X 732u
#define MAIN_DISPLAY_TREND_STATS_W \
    (MAIN_DISPLAY_INFO_RIGHT - MAIN_DISPLAY_TREND_STATS_X)
#define MAIN_DISPLAY_TREND_STATS_NAME_W 60u
#define MAIN_DISPLAY_TREND_STATS_VALUE_W \
    (MAIN_DISPLAY_TREND_STATS_W - MAIN_DISPLAY_TREND_STATS_NAME_W)
#define MAIN_DISPLAY_TREND_STATS_ROW_H 30u
#define MAIN_DISPLAY_TREND_STATS_RANGE_Y 198u
#define MAIN_DISPLAY_TREND_STATS_MAX_Y 228u
#define MAIN_DISPLAY_TREND_STATS_MIN_Y 258u
#define MAIN_DISPLAY_TREND_STATS_AVG_Y 288u
#define MAIN_DISPLAY_X_LABEL_Y 298u
#define MAIN_DISPLAY_CHART_PANEL_Y MAIN_DISPLAY_TREND_Y
#define MAIN_DISPLAY_CHART_PANEL_H MAIN_DISPLAY_TREND_H
#define MAIN_DISPLAY_PLOT_DIVIDER_H 4u
#define MAIN_DISPLAY_PLOT_DIVIDER_Y MAIN_DISPLAY_TREND_Y
#define MAIN_DISPLAY_PLOT_BG_Y MAIN_DISPLAY_PLOT_Y
#define MAIN_DISPLAY_PLOT_BG_H MAIN_DISPLAY_PLOT_H

/* Axis gutters: Y gutter is an inset cell column (not floating text);
 * X gutter is a 22px bar with top hairline. Both breathe 6px/10px from the
 * plot to avoid the "thin贴边" look. */
#define MAIN_DISPLAY_Y_AXIS_X 0u
#define MAIN_DISPLAY_Y_AXIS_W MAIN_DISPLAY_PLOT_X
#define MAIN_DISPLAY_Y_AXIS_Y MAIN_DISPLAY_TREND_Y
#define MAIN_DISPLAY_Y_AXIS_H MAIN_DISPLAY_TREND_H
#define MAIN_DISPLAY_X_AXIS_X MAIN_DISPLAY_PLOT_X
#define MAIN_DISPLAY_X_AXIS_W MAIN_DISPLAY_PLOT_W
#define MAIN_DISPLAY_X_AXIS_Y MAIN_DISPLAY_X_LABEL_Y
#define MAIN_DISPLAY_X_AXIS_H 22u

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
    char function_line1[MAIN_DISPLAY_FUNCTION_MAX];
    char function_line2[MAIN_DISPLAY_FUNCTION_MAX];
    char brand[20];
    char temperature[12];
    char uptime[12];
    char active_status[MAIN_DISPLAY_META_MAX];
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
    float trend_stat_minimum;
    float trend_stat_maximum;
    float trend_stat_average;
    char trend_stat_minimum_text[MAIN_DISPLAY_AXIS_LABEL_MAX];
    char trend_stat_maximum_text[MAIN_DISPLAY_AXIS_LABEL_MAX];
    char trend_stat_average_text[MAIN_DISPLAY_AXIS_LABEL_MAX];
    /* Derived 1/2/5 axis geometry: the full trend rebuild (grid + labels)
     * is only needed when these change, not when raw min/max drift.
     * trend_minimum/trend_maximum remain DATA bounds; the projection onto
     * the stable axis is applied by the caller that owns residency. */
    float trend_axis_step;
    float trend_axis_top;
    char trend_axis_unit[8];
} main_display_frame_t;

/* Stable display-axis identity (the 1/2/5 geometry currently painted).
 * Carried explicitly across frames so a sliding-window drift inside one
 * unit never mints a new identity. */
typedef struct {
    bool valid;
    char unit[8];
    float step;
    float top;
} main_display_trend_axis_t;

main_display_layout_t main_display_layout_value(uint8_t value_len,
                                                 uint16_t start_x,
                                                 uint8_t slots);
uint16_t main_display_cursor_x(const main_display_layout_t *layout,
                               uint16_t cursor_pos);
uint16_t main_display_special_color(uint8_t special);
const char *main_display_rate_text(ui_rate_t rate);
const char *main_display_function_text(ui_function_t function);
void main_display_format(const ui_model_t *model, main_display_frame_t *frame);
void main_display_format_runtime(main_display_frame_t *frame,
                                  int16_t temperature_tenths_c,
                                  uint32_t uptime_ms);
/* Extracts the axis identity a frame was rendered with, for use as the
 * resident axis of the next frame. */
void main_display_get_trend_axis(const main_display_frame_t *frame,
                                 main_display_trend_axis_t *axis);
/* trend_minimum/trend_maximum are always left as the scaled window DATA
 * bounds. The frame carries a fresh 1/2/5 auto fit of the window; axis
 * stability (resident/candidate) is decided by trend_axis.c and the chosen
 * identity is projected back onto the frame with main_display_set_trend_axis(). */
void main_display_format_trend(const trend_buffer_t *trend, uint32_t now_ms,
                               const char *unit, main_display_frame_t *frame);
void main_display_set_trend_axis(main_display_frame_t *frame, float step,
                                 float top, const char *unit);
void main_display_format_linear_trend_labels(main_display_frame_t *frame);
uint8_t main_display_trend_plot_y(float value, float minimum, float maximum);
