/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "hal_board.h"
#include "font_digits.h"
#include "font_half.h"
#include "font_text.h"
#include "keypad.h"
#include "k2000_proto.h"
#include "lt7680_bus.h"
#include "lt7680_gfx.h"
#include "main_display.h"
#include "panel_transform.h"
#include "reading_split.h"
#include "render_scheduler.h"
#include "scene.h"
#include "ui_model.h"
#include "trend_buffer.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* SPI self-test: reset LT7680 then repeatedly write one register so that a
 * logic analyzer on PA5 (SCK) / PA7 (SDI) shows continuous SPI frames. It
 * deliberately skips panel init so the question "is LT7680 SPI reachable?" is
 * answered in isolation. Set to 1 to enable. */
#define LT7680_SPI_SELFTEST 0U

/* Demo feed: synthesize K2000 host frames on a timer so the full
 * UART->proto->reading_split->ui_model->trend_buffer->render pipeline can be
 * verified on the bench without an instrument. Values ramp up/down while the
 * unit/range table rotates (VDC/VAC/ADC/AAC/OHM/KOHM/MOHM/Hz/kHz/MHz/CEL plus
 * mV/mA variants), exercising the split DC/AC half-height suffix, the
 * digit-size unit letters and the info column lamps (REL/FILT/AUTO/MATH,
 * HOLD/TRIG, FAST/MED/SLOW rate). Units are limited to the 64x128 digit
 * charset (no U/Z/S glyphs; Flash too tight to add them). Set to 1 to enable;
 * excluded from the normal build so the Flash budget is unaffected. Keep the
 * unit table in sync with sim/index.html. */
#define K2000_DEMO_FEED 0U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static ui_model_t s_ui;
static keypad_t s_keypad;
static bool s_display_ready;
static bool s_display_enabled;
static uint8_t s_ui_dirty_regions;
static bool s_blink_visible = true;
static uint32_t s_blink_tick;
static trend_buffer_t s_trend;
static trend_column_t s_trend_columns[TREND_MAX_COLUMNS];
static uint8_t s_drawn_trend_y0[TREND_MAX_COLUMNS];
static uint8_t s_drawn_trend_y1[TREND_MAX_COLUMNS];
static uint8_t s_drawn_trend_occupied[(TREND_MAX_COLUMNS + 7u) / 8u];
static main_display_frame_t s_frame;
static uint32_t s_text_refresh_tick;
static uint32_t s_trend_refresh_tick;
static uint16_t s_render_column;
static uint8_t s_render_item;
static render_scheduler_t s_renderer;
static bool s_waiting_visible;

typedef struct
{
    const char *text;
    uint16_t x;
    uint16_t y;
    uint16_t color;
    uint16_t cx;
    uint8_t row;
    uint8_t col;
    uint8_t mode; /* 0 = text, 1 = digits (with unit symbols), 2 = half */
    bool active;
} bitmap_job_t;

static bitmap_job_t s_bitmap_job;

#if K2000_DEMO_FEED
/* One entry per demo "range". lo_mant/hi_mant are the ramp low/high mantissas
 * scaled by 10^frac_digits; status09 packs REL=0x40 FILT=0x20 AUTO=0x10,
 * status08 packs HOLD=0x10 TRIG=0x08 FAST=0x04 MED=0x02 SLOW=0x01. Mirrors
 * sim/index.html DEMO_UNITS. */
typedef struct
{
    const char *unit;
    uint8_t int_digits;
    uint8_t frac_digits;
    uint32_t lo_mant;
    uint32_t hi_mant;
    uint8_t status09;
    uint8_t status08;
} demo_unit_t;

static const demo_unit_t s_demo_units[] = {
    {"VDC", 2u, 5u, 20000u, 1250000u, 0x10u, 0x04u},
    {"VAC", 3u, 5u, 20000u, 7000000u, 0x30u, 0x02u},
    {"ADC", 2u, 5u, 10000u, 300000u, 0x10u, 0x01u},
    {"AAC", 2u, 5u, 10000u, 300000u, 0x40u, 0x12u},
    {"MVDC", 3u, 5u, 10000u, 100000u, 0x30u, 0x04u},
    {"MVAC", 3u, 5u, 10000u, 100000u, 0x10u, 0x01u},
    {"MADC", 2u, 5u, 10000u, 200000u, 0x50u, 0x04u},
    {"MAAC", 2u, 5u, 10000u, 200000u, 0x30u, 0x02u},
    {"OHM", 4u, 4u, 1000u, 2000000u, 0x10u, 0x02u},
    {"KOHM", 3u, 4u, 1000u, 1000000u, 0x50u, 0x01u},
    {"MOHM", 3u, 4u, 1000u, 1000000u, 0x30u, 0x0Cu},
    {"Hz", 3u, 3u, 1000u, 1000000u, 0x10u, 0x04u},
    {"kHz", 3u, 3u, 1000u, 1000000u, 0x00u, 0x14u},
    {"MHz", 2u, 3u, 1000u, 50000u, 0x40u, 0x02u},
    {"\xC2\xB0"
     "CEL",
     2u, 3u, 1000u, 50000u, 0x30u, 0x01u},
};
#define DEMO_UNIT_COUNT \
    ((uint8_t)(sizeof(s_demo_units) / sizeof(s_demo_units[0])))
#define DEMO_FEED_PERIOD_MS 100u
#define DEMO_SAMPLES_PER_UNIT 40u

static uint32_t s_demo_last_tick;
static uint32_t s_demo_sample;

static void demo_u32_to_padded(char *out, uint32_t v, uint8_t digits)
{
    uint8_t i = digits;
    while (i > 0u)
    {
        out[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    }
}

static void demo_format_value(const demo_unit_t *u, char *out)
{
    uint32_t div = 1u;
    uint32_t ph = s_demo_sample % DEMO_SAMPLES_PER_UNIT;
    uint32_t tri = ph < (DEMO_SAMPLES_PER_UNIT / 2u)
                       ? ph
                       : (DEMO_SAMPLES_PER_UNIT - ph);
    uint32_t mant = u->lo_mant +
                    (u->hi_mant - u->lo_mant) * tri * 2u / DEMO_SAMPLES_PER_UNIT;
    uint8_t i;
    for (i = 0u; i < u->frac_digits; i++)
    {
        div *= 10u;
    }
    demo_u32_to_padded(out, mant / div, u->int_digits);
    out[u->int_digits] = '.';
    demo_u32_to_padded(out + u->int_digits + 1u, mant % div, u->frac_digits);
    out[u->int_digits + u->frac_digits + 1u] = '\0';
}

static void demo_feed_unit(const char *unit)
{
    /* A leading UTF-8 micro (C2 B5) or degree (C2 B0) is emitted as its
     * inline symbol tag so the parser appends the symbol to the field instead
     * of treating 0xC2 as a new-field tag (>=0x80). */
    if (unit[0] == (char)0xC2u)
    {
        if (unit[1] == (char)0xB5u)
        {
            k2000_proto_feed(K2000_TAG_SYM_MICRO);
            unit += 2;
        }
        else if (unit[1] == (char)0xB0u)
        {
            k2000_proto_feed(K2000_TAG_SYM_DEGREE);
            unit += 2;
        }
    }
    for (; *unit != '\0'; unit++)
    {
        k2000_proto_feed((uint8_t)*unit);
    }
}

static void k2000_demo_feed(void)
{
    uint32_t now = HAL_GetTick();
    const demo_unit_t *u;
    char text[16];
    const char *p;
    uint8_t unit_index;

    if (now - s_demo_last_tick < DEMO_FEED_PERIOD_MS)
    {
        return;
    }
    s_demo_last_tick = now;

    unit_index = (uint8_t)((s_demo_sample / DEMO_SAMPLES_PER_UNIT) %
                           DEMO_UNIT_COUNT);
    u = &s_demo_units[unit_index];
    demo_format_value(u, text);

    /* 0x0D start, 0x01 field tag, value+unit text, then status tags. The
     * field terminator doubles as the first status tag (see parser). */
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x01u);
    for (p = text; *p != '\0'; p++)
    {
        k2000_proto_feed((uint8_t)*p);
    }
    demo_feed_unit(u->unit);
    k2000_proto_feed(K2000_TAG_STATUS_REL); /* 0x09 REL/FILT/AUTO */
    k2000_proto_feed(u->status09);
    k2000_proto_feed(K2000_TAG_STATUS_HOLD); /* 0x08 HOLD/TRIG/rate */
    k2000_proto_feed(u->status08);
    k2000_proto_feed(K2000_TAG_STATUS_SHIFT); /* 0x07 MATH lamp every 4th */
    k2000_proto_feed((unit_index % 4u == 3u) ? 0x20u : 0x00u);

    s_demo_sample++;
}
#endif /* K2000_DEMO_FEED */

static void display_enable_after_initial_frame(void)
{
    if (render_scheduler_take_initial_complete(&s_renderer) &&
        !s_display_enabled)
    {
        (void)lt7680_write_reg(0x12u, 0x48u);
        s_display_enabled = true;
        hal_uart_send_text("PASS initial frame enabled\r\n");
    }
}

static void proto_on_event(const k2000_event_t *evt)
{
    char num[UI_MODEL_MAX_FIELD];
    char unit[UI_MODEL_MAX_UNIT];
    uint8_t num_len;
    uint8_t unit_len;
    uint8_t special;

    if (evt == 0)
    {
        return;
    }
    switch (evt->type)
    {
    case K2000_EVT_FIELD:
        if (reading_is_special(evt->field.value, evt->field.value_len,
                               &special))
        {
            num_len = evt->field.value_len;
            if (num_len >= sizeof(num))
                num_len = (uint8_t)(sizeof(num) - 1u);
            memcpy(num, evt->field.value, num_len);
            num[num_len] = '\0';
            unit_len = 0u;
            unit[0] = '\0';
        }
        else
        {
            reading_split(evt->field.value, evt->field.value_len, num, &num_len,
                          unit, &unit_len);
            special = 0u;
        }
        ui_model_apply_reading(&s_ui, num, num_len, unit, unit_len, special);
        if (special == 0u)
        {
            (void)trend_buffer_add(&s_trend, HAL_GetTick(), num, unit);
        }
        s_ui_dirty_regions |= RENDER_DIRTY_READING;
        break;
    case K2000_EVT_STATUS:
        ui_model_apply_status(&s_ui, evt->status_tag, evt->status_value);
        s_ui_dirty_regions |= RENDER_DIRTY_STATUS | RENDER_DIRTY_READING;
        break;
    case K2000_EVT_CURSOR:
        ui_model_apply_cursor(&s_ui, evt->pos);
        s_ui_dirty_regions |= RENDER_DIRTY_READING;
        break;
    case K2000_EVT_BLINK_START:
        ui_model_apply_blink(&s_ui, true);
        s_ui_dirty_regions |= RENDER_DIRTY_READING;
        break;
    case K2000_EVT_BLINK_END:
        ui_model_apply_blink(&s_ui, false);
        s_ui_dirty_regions |= RENDER_DIRTY_READING;
        break;
    case K2000_EVT_SYMBOL:
        ui_model_apply_symbol(&s_ui, evt->ctrl);
        s_ui_dirty_regions |= RENDER_DIRTY_READING;
        break;
    case K2000_EVT_SEGMENT:
        ui_model_apply_segment(&s_ui, evt->ctrl);
        s_ui_dirty_regions |= RENDER_DIRTY_READING;
        break;
    case K2000_EVT_FLUSH:
        ui_model_apply_flush(&s_ui);
        s_ui_dirty_regions |= RENDER_DIRTY_READING;
        break;
    default:
        break;
    }
}

static void proto_on_unknown(uint8_t byte)
{
    (void)byte;
}

/* Reading scene (id 0). The renderer keeps the verified panel writes behind
 * the display-ready gate and uses the logical UI coordinate transform. */
static void reading_scene_enter(void)
{
}

static void reading_scene_exit(void)
{
}

static lt7680_status_t ui_fill_rect(uint16_t x, uint16_t y, uint16_t w,
                                    uint16_t h, uint16_t color)
{
    lt7680_rect_t rect;

    panel_transform_ui_rect_to_fb(x, y, w, h, &rect.x, &rect.y,
                                  &rect.w, &rect.h);
    return lt7680_gfx_fill_rect(&rect, color);
}

static lt7680_status_t ui_draw_line(uint16_t x0, uint16_t y0, uint16_t x1,
                                    uint16_t y1, uint16_t color)
{
    uint16_t fx0, fy0, fx1, fy1;
    panel_transform_ui_to_fb(x0, y0, &fx0, &fy0);
    panel_transform_ui_to_fb(x1, y1, &fx1, &fy1);
    return lt7680_gfx_draw_line((int16_t)fx0, (int16_t)fy0,
                                (int16_t)fx1, (int16_t)fy1, color);
}

static const uint8_t *text_glyph(const char *text, uint8_t *advance)
{
    const uint8_t *bitmap;
    *advance = 1u;
    if ((uint8_t)text[0] == 0xC2u && (uint8_t)text[1] == 0xB5u)
    {
        bitmap = font_text_symbol_bitmap(FONT_TEXT_SYM_MICRO);
        *advance = 2u;
    }
    else if ((uint8_t)text[0] == 0xC2u && (uint8_t)text[1] == 0xB0u)
    {
        bitmap = font_text_symbol_bitmap(FONT_TEXT_SYM_DEGREE);
        *advance = 2u;
    }
    else if ((uint8_t)text[0] == 0xC2u && (uint8_t)text[1] == 0xB1u)
    {
        bitmap = font_text_symbol_bitmap(FONT_TEXT_SYM_PLUS_MINUS);
        *advance = 2u;
    }
    else if ((uint8_t)text[0] == 0xCEu && (uint8_t)text[1] == 0xA9u)
    {
        bitmap = font_text_symbol_bitmap(FONT_TEXT_SYM_OHM);
        *advance = 2u;
    }
    else
        bitmap = font_text_bitmap(*text);
    return bitmap != 0 ? bitmap : font_text_bitmap('?');
}

/* The unit is rendered at digit size; it may contain the µ / ° / Ω symbols
 * (2-byte UTF-8) that live in the digit font's symbol table. */
static const uint8_t *digit_glyph(const char *text, uint8_t *advance)
{
    const uint8_t *bitmap;
    *advance = 1u;
    if ((uint8_t)text[0] == 0xC2u && (uint8_t)text[1] == 0xB5u)
    {
        bitmap = font_digit_symbol_bitmap(FONT_DIGIT_SYM_MICRO);
        *advance = 2u;
    }
    else if ((uint8_t)text[0] == 0xC2u && (uint8_t)text[1] == 0xB0u)
    {
        bitmap = font_digit_symbol_bitmap(FONT_DIGIT_SYM_DEGREE);
        *advance = 2u;
    }
    else if ((uint8_t)text[0] == 0xCEu && (uint8_t)text[1] == 0xA9u)
    {
        bitmap = font_digit_symbol_bitmap(FONT_DIGIT_SYM_OHM);
        *advance = 2u;
    }
    else
        bitmap = font_digit_bitmap(*text);
    return bitmap != 0 ? bitmap : font_digit_bitmap('?');
}

/* Draw at most twelve horizontal bitmap runs. A run maps to one transformed
 * GE rectangle, bounding every call independently of string/glyph size. */
static bool ui_draw_bitmap_slice(uint16_t x, uint16_t y, const char *text,
                                 uint16_t color, uint8_t mode)
{
    uint8_t budget = 12u;
    if (!s_bitmap_job.active)
    {
        s_bitmap_job.text = text;
        s_bitmap_job.x = x;
        s_bitmap_job.y = y;
        s_bitmap_job.color = color;
        s_bitmap_job.cx = x;
        s_bitmap_job.row = 0u;
        s_bitmap_job.col = 0u;
        s_bitmap_job.mode = mode;
        s_bitmap_job.active = true;
    }
    while (budget > 0u && *s_bitmap_job.text != '\0')
    {
        uint8_t advance = 1u;
        uint8_t width = FONT_TEXT_WIDTH;
        uint8_t height = FONT_TEXT_HEIGHT;
        uint8_t bpr = FONT_TEXT_BYTES_PER_ROW;
        const uint8_t *bitmap;
        if (s_bitmap_job.mode == 2u)
        {
            bitmap = font_half_bitmap(*s_bitmap_job.text);
            width = FONT_HALF_WIDTH;
            height = FONT_HALF_HEIGHT;
            bpr = FONT_HALF_BYTES_PER_ROW;
        }
        else if (s_bitmap_job.mode == 1u)
        {
            bitmap = digit_glyph(s_bitmap_job.text, &advance);
            width = FONT_DIGIT_WIDTH;
            height = FONT_DIGIT_HEIGHT;
            bpr = FONT_DIGIT_BYTES_PER_ROW;
        }
        else
        {
            bitmap = text_glyph(s_bitmap_job.text, &advance);
        }
        if (bitmap == 0)
        {
            s_bitmap_job.text += advance;
            continue;
        }
        while (s_bitmap_job.row < height)
        {
            const uint8_t *bits = bitmap + s_bitmap_job.row * bpr;
            while (s_bitmap_job.col < width &&
                   (bits[s_bitmap_job.col >> 3] & (uint8_t)(0x80u >> (s_bitmap_job.col & 7u))) == 0u)
                s_bitmap_job.col++;
            if (s_bitmap_job.col < width)
            {
                uint8_t start = s_bitmap_job.col;
                while (s_bitmap_job.col < width &&
                       (bits[s_bitmap_job.col >> 3] & (uint8_t)(0x80u >> (s_bitmap_job.col & 7u))) != 0u)
                    s_bitmap_job.col++;
                (void)ui_fill_rect((uint16_t)(s_bitmap_job.cx + start),
                                   (uint16_t)(s_bitmap_job.y + s_bitmap_job.row),
                                   (uint16_t)(s_bitmap_job.col - start), 1u,
                                   s_bitmap_job.color);
                budget--;
                if (budget == 0u)
                    return false;
            }
            else
            {
                s_bitmap_job.col = 0u;
                s_bitmap_job.row++;
            }
        }
        s_bitmap_job.row = 0u;
        s_bitmap_job.col = 0u;
        s_bitmap_job.cx = (uint16_t)(s_bitmap_job.cx + width);
        s_bitmap_job.text += advance;
    }
    s_bitmap_job.active = false;
    return true;
}

static bool ui_draw_text(uint16_t x, uint16_t y, const char *text,
                         uint16_t color)
{
    return ui_draw_bitmap_slice(x, y, text, color, 0u);
}

static bool ui_draw_digits(uint16_t x, uint16_t y, const char *text,
                           uint16_t color)
{
    return ui_draw_bitmap_slice(x, y, text, color, 1u);
}

static bool ui_draw_half(uint16_t x, uint16_t y, const char *text,
                         uint16_t color)
{
    return ui_draw_bitmap_slice(x, y, text, color, 2u);
}

#define DRAW_ITEM(call_) \
    do                   \
    {                    \
        if (!(call_))    \
            return;      \
        s_render_item++; \
    } while (0)

/* Right-align a pure-ASCII text line against an x edge (12px text advance). */
static uint16_t right_text_x(const char *text, uint16_t right_edge)
{
    return (uint16_t)(right_edge - (uint16_t)strlen(text) * FONT_TEXT_WIDTH);
}

static bool trend_drawn_occupied(uint16_t column)
{
    return (s_drawn_trend_occupied[column >> 3] &
            (uint8_t)(1u << (column & 7u))) != 0u;
}

static void trend_set_drawn(uint16_t column, bool occupied,
                            uint8_t y0, uint8_t y1)
{
    uint8_t mask = (uint8_t)(1u << (column & 7u));
    if (occupied)
        s_drawn_trend_occupied[column >> 3] |= mask;
    else
        s_drawn_trend_occupied[column >> 3] &= (uint8_t)~mask;
    s_drawn_trend_y0[column] = y0;
    s_drawn_trend_y1[column] = y1;
}

static void trend_restore_grid(uint16_t x0, uint16_t x1,
                               uint16_t y0, uint16_t y1)
{
    uint8_t i;
    for (i = 0u; i < MAIN_DISPLAY_Y_LABEL_COUNT; i++)
    {
        uint16_t y = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                i * MAIN_DISPLAY_PLOT_H / 3u);
        if (y >= y0 && y <= y1)
            (void)ui_draw_line(x0, y, x1, y, MAIN_DISPLAY_COLOR_GRID);
    }
    for (i = 0u; i < MAIN_DISPLAY_X_LABEL_COUNT; i++)
    {
        uint16_t x = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                                i * MAIN_DISPLAY_PLOT_W / 4u);
        if (x >= x0 && x <= x1)
            (void)ui_draw_line(x, y0, x, y1, MAIN_DISPLAY_COLOR_GRID);
    }
}

static uint16_t trend_y_label_y(uint8_t index)
{
    uint16_t axis_y = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                 index * MAIN_DISPLAY_PLOT_H / 3u);
    uint16_t label_y = axis_y > 8u ? (uint16_t)(axis_y - 8u) : 0u;

    /* The first label is centred near the top grid line, but its bitmap must
     * remain inside the resident trend region. Otherwise a periodic axis
     * refresh erases the bottom of the reading band at y=188..191. */
    return label_y < MAIN_DISPLAY_TREND_Y ? MAIN_DISPLAY_TREND_Y : label_y;
}

static void trend_draw_column(uint16_t column, bool erase_previous)
{
    trend_column_t *c = &s_trend_columns[column];
    uint16_t x = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                            (uint32_t)column * MAIN_DISPLAY_PLOT_W / TREND_MAX_COLUMNS);
    uint16_t x0 = x > MAIN_DISPLAY_PLOT_X ? (uint16_t)(x - 1u) : x;
    uint16_t x1 = x < MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W ? (uint16_t)(x + 1u) : x;
    uint8_t y0 = 0u, y1 = 0u;

    if (erase_previous && trend_drawn_occupied(column))
    {
        uint16_t old_y0 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     s_drawn_trend_y0[column]);
        uint16_t old_y1 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     s_drawn_trend_y1[column]);
        (void)ui_fill_rect(x0, old_y0, (uint16_t)(x1 - x0 + 1u),
                           (uint16_t)(old_y1 - old_y0 + 1u),
                           MAIN_DISPLAY_COLOR_BG);
        trend_restore_grid(x0, x1, old_y0, old_y1);
    }
    if (c->occupied && s_frame.trend_has_data)
    {
        float span = s_frame.trend_maximum - s_frame.trend_minimum;
        y0 = (uint8_t)((s_frame.trend_maximum - c->maximum) *
                       MAIN_DISPLAY_PLOT_H / span);
        y1 = (uint8_t)((s_frame.trend_maximum - c->minimum) *
                       MAIN_DISPLAY_PLOT_H / span);
        if (x > MAIN_DISPLAY_PLOT_X)
            (void)ui_draw_line((uint16_t)(x - 1u),
                               (uint16_t)(MAIN_DISPLAY_PLOT_Y + y0),
                               (uint16_t)(x - 1u),
                               (uint16_t)(MAIN_DISPLAY_PLOT_Y + y1),
                               MAIN_DISPLAY_COLOR_GREEN_DIM);
        (void)ui_draw_line(x, (uint16_t)(MAIN_DISPLAY_PLOT_Y + y0), x,
                           (uint16_t)(MAIN_DISPLAY_PLOT_Y + y1),
                           MAIN_DISPLAY_COLOR_GREEN);
        if (x < MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W)
            (void)ui_draw_line((uint16_t)(x + 1u),
                               (uint16_t)(MAIN_DISPLAY_PLOT_Y + y0),
                               (uint16_t)(x + 1u),
                               (uint16_t)(MAIN_DISPLAY_PLOT_Y + y1),
                               MAIN_DISPLAY_COLOR_GREEN_DIM);
    }
    trend_set_drawn(column, c->occupied && s_frame.trend_has_data, y0, y1);
}

static void reading_scene_render(void)
{
    uint32_t now = HAL_GetTick();
    bool initial_phase;
    bool text_due;
    bool trend_due;

    if (!s_display_ready)
    {
        return;
    }
    trend_buffer_update(&s_trend, now);
    /* Publish immutable render snapshots only while idle. Queue a due trend
     * before text regions so a continuously dirty 10 Hz reading cannot starve
     * the 5 Hz graph; the region request is retained by the scheduler and runs
     * immediately after the two incremental trend phases. Active work is never
     * restarted, so every bitmap/graph slice progresses at 500 readings/s. */
    if (s_renderer.phase == RENDER_PHASE_IDLE)
    {
        text_due = s_ui_dirty_regions != 0u &&
                   (now - s_text_refresh_tick) >= 100u;
        trend_due = (now - s_trend_refresh_tick) >= 200u;
        if (text_due)
        {
            main_display_format(&s_ui, &s_frame);
            s_text_refresh_tick = now;
        }
        if (trend_due)
        {
            (void)trend_buffer_project(&s_trend, now, s_trend_columns,
                                       TREND_MAX_COLUMNS);
            main_display_format_trend(&s_trend, now, s_frame.unit, &s_frame);
            s_trend_refresh_tick = now;
            s_render_item = 0u;
            s_render_column = 0u;
            render_scheduler_request_trend(&s_renderer);
        }
        if (text_due)
        {
            render_scheduler_request_regions(&s_renderer, s_ui_dirty_regions);
            s_ui_dirty_regions = 0u;
        }
    }
    initial_phase = s_renderer.phase <= RENDER_PHASE_INITIAL_TREND_COLUMNS;
    /* One bounded region/slice per call. RX and keypad are serviced between
     * calls by the main loop. Routine phases clear only their own local band. */
    switch (s_renderer.phase)
    {
    case RENDER_PHASE_INITIAL_STATUS:
    case RENDER_PHASE_UPDATE_STATUS:
    {
        if (s_render_item == 0u)
        {
            (void)ui_fill_rect(0u, 0u, 960u, 24u, MAIN_DISPLAY_COLOR_BAR);
            s_render_item++;
            return;
        }
        /* Keep the protocol indicators in the approved first-line order.
         * BUFFER is a state indicator here; the capacity wording remains in
         * frame.buffer for adapters with enough horizontal space. */
        switch (s_render_item)
        {
        case 1u:
            DRAW_ITEM(ui_draw_text(8u, 0u, "REM", s_frame.status_active[0] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED));
            return;
        case 2u:
            DRAW_ITEM(ui_draw_text(62u, 0u, "TALK", s_frame.status_active[1] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED));
            return;
        case 3u:
            DRAW_ITEM(ui_draw_text(128u, 0u, "LSTN", s_frame.status_active[2] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED));
            return;
        case 4u:
            DRAW_ITEM(ui_draw_text(194u, 0u, "SRQ", s_frame.status_active[3] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED));
            return;
        case 5u:
            DRAW_ITEM(ui_draw_text(278u, 0u, s_frame.buffer, s_frame.status_active[10] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED));
            return;
        case 6u:
            DRAW_ITEM(ui_draw_text(566u, 0u, s_frame.gpib, MAIN_DISPLAY_COLOR_MUTED));
            return;
        case 7u:
            DRAW_ITEM(ui_draw_text(710u, 0u, "CONT", MAIN_DISPLAY_COLOR_GREEN));
            return;
        case 8u:
            DRAW_ITEM(ui_draw_text(788u, 0u, "TRIG", s_frame.status_active[5] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED));
            return;
        default:
            render_scheduler_complete_phase(&s_renderer);
            s_render_item = 0u;
            return;
        }
    }
    case RENDER_PHASE_INITIAL_READING:
    case RENDER_PHASE_UPDATE_READING:
    {
        /* The reading band owns y24..192: left-aligned value at digit size,
         * unit at digit size, half-height DC/AC suffix, and the right-aligned
         * info column (Zin / Range / Rate / FILT REL MATH). */
        uint8_t first_info;
        if (s_render_item == 0u)
        {
            (void)ui_fill_rect(0u, MAIN_DISPLAY_READING_Y, 960u,
                               MAIN_DISPLAY_READING_H, MAIN_DISPLAY_COLOR_BG);
            s_render_item++;
            return;
        }
        if (s_frame.no_data)
        {
            if (s_render_item == 1u)
            {
                DRAW_ITEM(ui_draw_text(MAIN_DISPLAY_READING_X, 84u,
                                       "WAITING FOR DATA",
                                       MAIN_DISPLAY_COLOR_MUTED));
                return;
            }
            first_info = 2u;
        }
        else
        {
            if (s_render_item == 1u)
            {
                DRAW_ITEM(ui_draw_digits(s_frame.start_x, s_frame.reading_y,
                                         s_frame.value, s_frame.value_color));
                return;
            }
            if (s_render_item == 2u)
            {
                DRAW_ITEM(ui_draw_digits(s_frame.end_x, s_frame.reading_y,
                                         s_frame.unit, s_frame.value_color));
                return;
            }
            if (s_render_item == 3u && s_frame.unit_suffix[0] != '\0')
            {
                DRAW_ITEM(ui_draw_half(
                    (uint16_t)(s_frame.end_x +
                               (uint16_t)s_frame.unit_len * FONT_DIGIT_WIDTH),
                    MAIN_DISPLAY_DCAC_Y, s_frame.unit_suffix,
                    s_frame.value_color));
                return;
            }
            first_info = s_frame.unit_suffix[0] != '\0' ? 4u : 3u;
        }
        if (s_render_item == first_info)
        {
            DRAW_ITEM(ui_draw_text(
                right_text_x(s_frame.impedance, MAIN_DISPLAY_INFO_RIGHT),
                MAIN_DISPLAY_INFO_ZIN_Y, s_frame.impedance,
                MAIN_DISPLAY_COLOR_MUTED));
            return;
        }
        if (s_render_item == first_info + 1u)
        {
            DRAW_ITEM(ui_draw_text(
                right_text_x(s_frame.range, MAIN_DISPLAY_INFO_RIGHT),
                MAIN_DISPLAY_INFO_RANGE_Y, s_frame.range,
                MAIN_DISPLAY_COLOR_WHITE));
            return;
        }
        if (s_render_item == first_info + 2u)
        {
            DRAW_ITEM(ui_draw_text(
                right_text_x(s_frame.rate, MAIN_DISPLAY_INFO_RIGHT),
                MAIN_DISPLAY_INFO_RATE_Y, s_frame.rate,
                MAIN_DISPLAY_COLOR_WHITE));
            return;
        }
        if (s_render_item == first_info + 3u)
        {
            DRAW_ITEM(ui_draw_text(748u, MAIN_DISPLAY_INFO_STATUS_Y, "FILT",
                                   s_frame.status_active[7] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED));
            return;
        }
        if (s_render_item == first_info + 4u)
        {
            DRAW_ITEM(ui_draw_text(820u, MAIN_DISPLAY_INFO_STATUS_Y, "REL",
                                   s_frame.status_active[6] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED));
            return;
        }
        if (s_render_item == first_info + 5u)
        {
            DRAW_ITEM(ui_draw_text(892u, MAIN_DISPLAY_INFO_STATUS_Y, "MATH",
                                   s_frame.status_active[11] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED));
            return;
        }
        render_scheduler_complete_phase(&s_renderer);
        s_render_item = 0u;
        return;
    }
    case RENDER_PHASE_INITIAL_TREND_STATIC:
    {
        if (s_render_item == 0u)
        {
            (void)ui_fill_rect(0u, MAIN_DISPLAY_TREND_Y, 960u, 128u, MAIN_DISPLAY_COLOR_BG);
            s_render_item++;
            return;
        }
        if (s_render_item >= 1u && s_render_item <= 4u)
        {
            uint8_t i = (uint8_t)(s_render_item - 1u);
            uint16_t y = (uint16_t)(MAIN_DISPLAY_PLOT_Y + i * MAIN_DISPLAY_PLOT_H / 3u);
            if (!ui_draw_text(4u, trend_y_label_y(i), s_frame.y_labels[i], MAIN_DISPLAY_COLOR_CYAN))
                return;
            (void)ui_draw_line(MAIN_DISPLAY_PLOT_X, y, MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W, y, MAIN_DISPLAY_COLOR_GRID);
            s_render_item++;
            return;
        }
        if (s_render_item >= 5u && s_render_item <= 9u)
        {
            uint8_t i = (uint8_t)(s_render_item - 5u);
            uint16_t x = (uint16_t)(MAIN_DISPLAY_PLOT_X + i * MAIN_DISPLAY_PLOT_W / 4u);
            if (!ui_draw_text((uint16_t)(x > 24u ? x - 24u : x), MAIN_DISPLAY_X_LABEL_Y, s_frame.x_labels[i], MAIN_DISPLAY_COLOR_CYAN))
                return;
            (void)ui_draw_line(x, MAIN_DISPLAY_PLOT_Y, x, MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H, MAIN_DISPLAY_COLOR_GRID);
            s_render_item++;
            return;
        }
        render_scheduler_complete_phase(&s_renderer);
        s_render_item = 0u;
        return;
    }
    case RENDER_PHASE_UPDATE_TREND_AXES:
    {
        /* Axis text owns x=0..95 only; the plot and resident grid start at 96.
         * Pair each local clear with its replacement before moving to the next
         * label. Thus a shorter label cannot leave stale pixels, while the
         * complete axis never disappears for several cooperative slices. */
        if (s_render_item < MAIN_DISPLAY_Y_LABEL_COUNT * 2u &&
            (s_render_item & 1u) == 0u)
        {
            uint8_t i = (uint8_t)(s_render_item / 2u);
            (void)ui_fill_rect(0u, trend_y_label_y(i),
                               MAIN_DISPLAY_PLOT_X, FONT_TEXT_HEIGHT,
                               MAIN_DISPLAY_COLOR_BG);
            s_render_item++;
            return;
        }
        if (s_render_item < MAIN_DISPLAY_Y_LABEL_COUNT * 2u)
        {
            uint8_t i = (uint8_t)(s_render_item / 2u);
            if (!ui_draw_text(4u, trend_y_label_y(i), s_frame.y_labels[i],
                              MAIN_DISPLAY_COLOR_CYAN))
                return;
            s_render_item++;
            return;
        }
        render_scheduler_complete_phase(&s_renderer);
        s_render_item = 0u;
        return;
    }
    case RENDER_PHASE_INITIAL_TREND_COLUMNS:
    case RENDER_PHASE_UPDATE_TREND_COLUMNS:
    {
        uint16_t budget = 3u;
        if (s_frame.trend_has_data && s_waiting_visible)
        {
            (void)ui_fill_rect(390u, 232u, 192u, FONT_TEXT_HEIGHT,
                               MAIN_DISPLAY_COLOR_BG);
            trend_restore_grid(390u, 581u, 232u,
                               (uint16_t)(232u + FONT_TEXT_HEIGHT - 1u));
            s_waiting_visible = false;
            return;
        }
        while (s_render_column < TREND_MAX_COLUMNS && budget-- > 0u)
        {
            trend_draw_column(s_render_column, !initial_phase);
            s_render_column++;
        }
        if (s_render_column >= TREND_MAX_COLUMNS)
        {
            s_render_column = 0u;
            if (!s_frame.trend_has_data && !s_waiting_visible)
            {
                if (!ui_draw_text(390u, 232u, "WAITING FOR DATA",
                                  MAIN_DISPLAY_COLOR_MUTED))
                    return;
                s_waiting_visible = true;
            }
            render_scheduler_complete_phase(&s_renderer);
            s_render_item = 0u;
            /* The last hidden initial slice is now committed. Do not let
             * continuously arriving host updates postpone first reveal. */
            display_enable_after_initial_frame();
        }
        return;
    }
    case RENDER_PHASE_IDLE:
    default:
        break;
    }
    display_enable_after_initial_frame();
}

static const scene_t s_reading_scene = {
    reading_scene_enter,
    reading_scene_exit,
    reading_scene_render,
};

static void update_blink(void)
{
    uint32_t now = HAL_GetTick();

    if (!s_ui.blink)
    {
        s_blink_visible = true;
        s_blink_tick = now;
        return;
    }
    if ((now - s_blink_tick) >= 250u)
    {
        s_blink_tick = now;
        s_blink_visible = !s_blink_visible;
        s_ui_dirty_regions |= RENDER_DIRTY_READING;
    }
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{

    /* USER CODE BEGIN 1 */
    ui_model_init(&s_ui);
    trend_buffer_init(&s_trend);
    render_scheduler_init(&s_renderer);
    keypad_init(&s_keypad);
    scene_mgr_init();
    scene_mgr_register(0, &s_reading_scene);
    scene_mgr_enter(0);
    s_ui_dirty_regions = RENDER_DIRTY_STATUS | RENDER_DIRTY_READING;

    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* USER CODE BEGIN Init */

    /* USER CODE END Init */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
/* USER CODE BEGIN 2 */
#if LT7680_SPI_SELFTEST
    hal_board_init();
    hal_uart_send_text("\r\nSPI SELFTEST\r\n");
    (void)lt7680_reset();
    for (;;)
    {
        /* Write reg 0x01 = 0x08, then read it back, then read status. Returning
         * data on PA6 (MISO) proves LT7680 is alive and receiving. */
        uint8_t rd = 0u;
        (void)lt7680_write_reg(0x01u, 0x08u);
        (void)lt7680_read_reg(0x01u, &rd);
        (void)lt7680_wait_ready(10u);
        (void)lt7680_delay_ms(1u);
    }
#else
    {
#if PANEL_LANDSCAPE
        const lt7680_panel_t panel = {960u, 320u, 16u};
#else
        const lt7680_panel_t panel = {320u, 960u, 16u};
#endif
        const k2000_proto_cb_t proto_cb = {proto_on_event, proto_on_unknown};
        lt7680_status_t st;
        uint8_t status = 0u;

        hal_board_init();
        k2000_proto_init(&proto_cb);
        panel_transform_init(MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_UI_HEIGHT,
                             panel.width, panel.height);
        hal_uart_send_text("\r\nK2000 TFT build13 trend-layout");
#if K2000_DEMO_FEED
        hal_uart_send_text(" DEMO-FEED\r\n");
#else
        hal_uart_send_text("\r\n");
#endif
        hal_uart_send_text("\r\nLT7680 SELF-TEST\r\n");

        st = lt7680_reset();
        if (st != LT7680_OK)
        {
            hal_uart_send_text("FAIL reset=");
            hal_uart_send_hex8((uint8_t)st);
            hal_uart_send_text("\r\n");
        }
        else
        {
            st = lt7680_read_status(&status);
            if (st != LT7680_OK)
            {
                hal_uart_send_text("FAIL status-read=");
                hal_uart_send_hex8((uint8_t)st);
                hal_uart_send_text("\r\n");
            }
            else
            {
                hal_uart_send_text("STATUS=0x");
                hal_uart_send_hex8(status);
                hal_uart_send_text("\r\n");
                st = lt7680_wait_ready(1000u);
                if (st != LT7680_OK)
                {
                    hal_uart_send_text("FAIL ready=");
                    hal_uart_send_hex8((uint8_t)st);
                    hal_uart_send_text("\r\n");
                }
                else
                {
                    /* Blank the display right after reset so the LT7680's default
                     * register state (colour-bar test pattern / display on) never
                     * flashes during the ~200ms panel init or gfx init. 0x08 = init
                     * display ctrl value (bit3 scan dir set, bits7/6/5/4/0-2 clear). */
                    (void)lt7680_write_reg(0x12u, 0x08u);
                    hal_panel_init();
                    st = lt7680_gfx_init(&panel);
                    if (st != LT7680_OK)
                    {
                        hal_uart_send_text("FAIL init=");
                        hal_uart_send_hex8((uint8_t)st);
                        hal_uart_send_text("\r\n");
                    }
                    else
                    {
                        /* Keep the display blank while SDRAM is cleared. Without this
                         * clear, REG[12h]=0x48 exposes stale/uninitialized canvas pixels
                         * as sparse RGB corruption. */
                        st = lt7680_gfx_clear(0x0000u);
                        if (st != LT7680_OK)
                        {
                            hal_uart_send_text("FAIL clear=");
                            hal_uart_send_hex8((uint8_t)st);
                            hal_uart_send_text("\r\n");
                        }
                        else
                        {
                            /* Build the complete first frame while REG[12h] remains 0x08.
                             * reading_scene_render() enables 0x48 once, only after its
                             * cooperative initial phases have all completed. */
                            main_display_format(&s_ui, &s_frame);
                            main_display_format_trend(&s_trend, HAL_GetTick(), s_frame.unit,
                                                      &s_frame);
                            (void)trend_buffer_project(&s_trend, HAL_GetTick(),
                                                       s_trend_columns, TREND_MAX_COLUMNS);
                            s_ui_dirty_regions = 0u;
                            hal_uart_send_text("PASS framebuffer ready, building hidden frame\r\n");
                            s_display_ready = true;
                        }
                    }
                }
            }
        }
    }
#endif /* LT7680_SPI_SELFTEST */
       /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
#if !K2000_DEMO_FEED
        {
            uint16_t rx_budget = 128u;
            int ch;
            while (rx_budget-- > 0u && (ch = hal_uart_receive_byte()) >= 0)
                k2000_proto_feed((uint8_t)ch);
        }
#else
        /* Demo mode is a no-host bench mode: skip UART RX so a floating RX
         * line (no host cable) cannot inject noise bytes into the parser. */
#endif
#if K2000_DEMO_FEED
        k2000_demo_feed();
#endif

        /* Scan the key matrix, debounce, and passthrough press/release codes to
         * the host. Local-key interpretation (DISPLAY/TREND scene switching) is
         * deferred to the trend milestone (ADR-0002). */
        {
            int code = keypad_scan(&s_keypad, hal_keypad_read_code(),
                                   HAL_GetTick());
            if (code != 0)
            {
                uint8_t b = (uint8_t)code;
                hal_uart_send(&b, 1);
            }
        }

        update_blink();
        scene_mgr_render();
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
