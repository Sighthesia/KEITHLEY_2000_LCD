#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font_digits.h"
#include "font_text.h"
#include "status_bar.h"
#include "ui_model.h"

/* Reading-page layout + host-safe render description (milestone-1).
 * PURE LOGIC ONLY: this module computes positions and the strings to draw;
 * it never touches the LT7680 / panel. The hardware renderer (main.c) feeds
 * the produced frame to lt7680_gfx_draw_text / font_digit_bitmap and maps
 * coordinates through panel_transform (ADR-0001).
 *
 * All views are authored in the logical 960x320 landscape space (ADR-0001).
 * Vertical bands, top -> bottom:
 *   status bar   y = 0    .. 24   (12x24 text: HOLD/REM/REL/TRIG/AUTO/ERR)
 *   unit row     y = 24   .. 48   (12x24 text, right-aligned to UI width)
 *   big reading  y = 48   .. 144  (48x96 digits, right-aligned)
 *   footer       y = 144  .. 320  (cursor underline / secondary info) */
#define MAIN_DISPLAY_UI_WIDTH 960u
#define MAIN_DISPLAY_UI_HEIGHT 320u

#define MAIN_DISPLAY_STATUS_BAR_Y 0u
#define MAIN_DISPLAY_STATUS_BAR_H FONT_TEXT_HEIGHT
#define MAIN_DISPLAY_UNIT_ROW_Y \
    (MAIN_DISPLAY_STATUS_BAR_Y + MAIN_DISPLAY_STATUS_BAR_H)
#define MAIN_DISPLAY_UNIT_ROW_H FONT_TEXT_HEIGHT
#define MAIN_DISPLAY_READING_Y \
    (MAIN_DISPLAY_UNIT_ROW_Y + MAIN_DISPLAY_UNIT_ROW_H)
#define MAIN_DISPLAY_READING_H FONT_DIGIT_HEIGHT
#define MAIN_DISPLAY_FOOTER_Y (MAIN_DISPLAY_READING_Y + MAIN_DISPLAY_READING_H)
#define MAIN_DISPLAY_FOOTER_H (MAIN_DISPLAY_UI_HEIGHT - MAIN_DISPLAY_FOOTER_Y)

/* Big-reading band spans the whole UI width, so 960/48 = 20 digit slots. */
#define MAIN_DISPLAY_READING_X 0u
#define MAIN_DISPLAY_MAX_SLOTS (MAIN_DISPLAY_UI_WIDTH / FONT_DIGIT_WIDTH)
#define MAIN_DISPLAY_UNIT_MAX_SLOTS (MAIN_DISPLAY_UI_WIDTH / FONT_TEXT_WIDTH)

/* No-data state (startup): the reading band shows the "NO DATA" hint instead
 * of digits/underscores. 12x24 small text, horizontally centred in the band
 * and vertically centred in it (Q2/Q6), in the same grey as "----". */
#define MAIN_DISPLAY_NO_DATA_TEXT "NO DATA"
#define MAIN_DISPLAY_NO_DATA_LEN 7u
#define MAIN_DISPLAY_NO_DATA_COLOR 0xC618u

/* Static decorations (Q5-a): one 1px dark-grey separator under the status
 * bar and one under the unit row, full UI width. Drawn with every frame
 * (they sit under the per-frame background clears), visually static. */
#define MAIN_DISPLAY_SEP_Y_STATUS MAIN_DISPLAY_UNIT_ROW_Y
#define MAIN_DISPLAY_SEP_Y_UNIT MAIN_DISPLAY_READING_Y
#define MAIN_DISPLAY_SEP_H 1u
#define MAIN_DISPLAY_SEP_COLOR 0x8410u

#define MAIN_DISPLAY_CURSOR_GAP 4u
#define MAIN_DISPLAY_CURSOR_Y \
    (MAIN_DISPLAY_READING_Y + MAIN_DISPLAY_READING_H + MAIN_DISPLAY_CURSOR_GAP)
#define MAIN_DISPLAY_CURSOR_H 4u
#define MAIN_DISPLAY_STATUS_LABEL_GAP 4u
#define MAIN_DISPLAY_STATUS_LABEL_MAX 6u

/* Right-aligned layout of a value into `slots` digit cells starting at
 * `start_x`. start_x is the left edge of the whole slot area; the returned
 * start_x is the left edge of the first glyph so the value ends flush at
 * start_x + slots*FONT_DIGIT_WIDTH. valid is 0 when value_len > slots. */
typedef struct {
    uint16_t start_x;
    uint16_t end_x;
    uint8_t valid;
} main_display_layout_t;

/* Value/unit/special/status fully computed from the model: everything the
 * hardware renderer needs, with no dependency on any HAL or LT7680 call. */
typedef struct {
    char value[UI_MODEL_MAX_FIELD];
    uint8_t value_len;
    char unit[UI_MODEL_MAX_UNIT];
    uint8_t unit_len;
    uint8_t special;          /* 0 normal, 1 OVERFLOW, 2 no-reading */
    bool no_data;             /* startup: no host message yet; show "NO DATA" */
    uint16_t no_data_x;       /* left edge of the "NO DATA" hint */
    uint16_t no_data_y;       /* top of the "NO DATA" hint */
    uint16_t value_color;     /* RGB565 for the big digits (special aware) */
    uint16_t start_x;         /* left edge of the right-aligned value block */
    uint16_t end_x;           /* right edge (exclusive) */
    uint16_t reading_y;       /* top of the big-reading band */
    uint16_t unit_x;          /* left edge of the unit text */
    uint16_t unit_y;          /* top of the unit row */
    bool cursor_visible;      /* blink set and a valid value present */
    uint16_t cursor_x;        /* left edge of the cursor slot */
    uint16_t cursor_y;        /* top of the cursor underline */
    uint16_t status_y;        /* top of the status bar */
    uint8_t status_count;     /* active core indicators to draw */
    char status_text[STATUS_BAR_CORE_COUNT][MAIN_DISPLAY_STATUS_LABEL_MAX];
} main_display_frame_t;

main_display_layout_t main_display_layout_value(uint8_t value_len,
                                                uint16_t start_x,
                                                uint8_t slots);
uint16_t main_display_cursor_x(const main_display_layout_t *layout,
                               uint16_t cursor_pos);
uint16_t main_display_special_color(uint8_t special);
void main_display_format(const ui_model_t *m, main_display_frame_t *f);
