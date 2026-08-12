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
 *   top band    y = 0    .. 24   (12x24 text: unit/range left, status right)
 *   big reading y = 24   .. 120  (48x96 digits, right-aligned)
 *   footer      y = 120  .. 320  (cursor underline / secondary info) */
#define MAIN_DISPLAY_UI_WIDTH 960u
#define MAIN_DISPLAY_UI_HEIGHT 320u

/* Top band merges the former status bar and unit row: the unit/range text
 * sits at the left edge, the lit status indicators right-aligned. */
#define MAIN_DISPLAY_TOP_BAND_Y 0u
#define MAIN_DISPLAY_TOP_BAND_H FONT_TEXT_HEIGHT
#define MAIN_DISPLAY_READING_Y \
    (MAIN_DISPLAY_TOP_BAND_Y + MAIN_DISPLAY_TOP_BAND_H)
#define MAIN_DISPLAY_READING_H FONT_DIGIT_HEIGHT
#define MAIN_DISPLAY_FOOTER_Y (MAIN_DISPLAY_READING_Y + MAIN_DISPLAY_READING_H)
#define MAIN_DISPLAY_FOOTER_H (MAIN_DISPLAY_UI_HEIGHT - MAIN_DISPLAY_FOOTER_Y)

/* Big-reading band spans the whole UI width, so 960/48 = 20 digit slots. */
#define MAIN_DISPLAY_READING_X 0u
#define MAIN_DISPLAY_MAX_SLOTS (MAIN_DISPLAY_UI_WIDTH / FONT_DIGIT_WIDTH)
#define MAIN_DISPLAY_UNIT_MAX_SLOTS (MAIN_DISPLAY_UI_WIDTH / FONT_TEXT_WIDTH)

/* Placeholder prompts (shared grey): seven right-aligned '?' slots in the
 * reading band while no host message has arrived yet, and the "Range ?"
 * unit/range hint while no unit has been received. */
#define MAIN_DISPLAY_NO_DATA_SLOTS 7u
#define MAIN_DISPLAY_PLACEHOLDER_COLOR 0xC618u
#define MAIN_DISPLAY_RANGE_PLACEHOLDER "Range ?"

/* Static decoration (Q5-a): one 1px dark-grey separator under the top band,
 * full UI width. Drawn with every frame (it sits under the per-frame
 * background clears), visually static. */
#define MAIN_DISPLAY_SEP_Y_TOP_BAND MAIN_DISPLAY_READING_Y
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
    bool no_data;             /* startup: no host message yet; show '?' slots */
    uint16_t no_data_x;       /* left edge of the right-aligned '?' slot block */
    uint16_t no_data_y;       /* top of the '?' slot block */
    uint16_t value_color;     /* RGB565 for the big digits (special aware) */
    uint16_t start_x;         /* left edge of the right-aligned value block */
    uint16_t end_x;           /* right edge (exclusive) */
    uint16_t reading_y;       /* top of the big-reading band */
    bool unit_placeholder;    /* unit/range empty: show "Range ?" */
    uint16_t unit_x;          /* left edge of the unit text (top band) */
    uint16_t unit_y;          /* top of the top band */
    bool cursor_visible;      /* blink set and a valid value present */
    uint16_t cursor_x;        /* left edge of the cursor slot */
    uint16_t cursor_y;        /* top of the cursor underline */
    uint16_t status_y;        /* top of the status block (top band) */
    uint16_t status_x;        /* left edge of the right-aligned status block */
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
