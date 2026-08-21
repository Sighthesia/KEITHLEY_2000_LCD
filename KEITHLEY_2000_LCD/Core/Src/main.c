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
#include "rif_reader.h"
#include "rif_tile_cache.h"
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

/* Cached-tile BTE renderer enabled after the off-screen cache write/read
 * probe and the 4x4 BTE visual block both passed hardware acceptance. */
#ifndef RIF_BTE_RENDERER
#define RIF_BTE_RENDERER 1U
#endif

/* Demo feed: synthesize K2000 host frames on a timer so the full
 * UART->proto->reading_split->ui_model->trend_buffer->render pipeline can be
 * verified on the bench without an instrument. Values ramp up/down while the
 * unit/range table rotates (VDC/VAC/ADC/AAC/mVDC/mVAC/mADC/mAAC/OHM/kOHM/MOHM/
 * Hz/kHz/MHz/CEL), exercising the split DC/AC half-height suffix, the
 * digit-size unit letters and the info panel lamps (REL/FILT/AUTO/MATH,
 * HOLD/TRIG, FAST/MED/SLOW rate). Units are limited to the 64x128 digit
 * charset (no U/Z/S glyphs; Flash too tight to add them). The DC/AC suffix is
 * the only half-height text; the mV/mA base units stay at digit size.
 * Set to 1 to enable; excluded from the normal build so the Flash budget is
 * unaffected. Keep the unit table in sync with sim/index.html. */
#define K2000_DEMO_FEED 1U

/* The sample clock and the display clock are deliberately independent. */
#define DEMO_SAMPLE_PERIOD_MS 2u
#define DISPLAY_FRAME_PERIOD_MS 33u

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
static bool s_initial_page_pending;
static bool s_frame_rendering;
static uint8_t s_visible_page;
static uint8_t s_render_page;
static uint8_t s_ready_page_mask;
static bool s_render_full_page;
static uint8_t s_text_generation;
static uint8_t s_page_text_generation[2];
static uint8_t s_frame_text_generation;
static bool s_frame_has_trend_update;
static bool s_render_status_regions;

static uint8_t s_ui_dirty_regions;
static bool s_blink_visible = true;
static uint32_t s_blink_tick;
static trend_buffer_t s_trend;
static trend_column_t s_trend_columns[TREND_MAX_COLUMNS];
/* Each SDRAM page owns its own dynamic curve. Keep the last rasterized
 * projection per page so a stable axis only repaints columns that changed
 * since this particular page was last visible. */
static uint8_t s_drawn_trend_y0[2][TREND_MAX_COLUMNS];
static uint8_t s_drawn_trend_y1[2][TREND_MAX_COLUMNS];
static uint8_t s_drawn_trend_occupied[2][(TREND_MAX_COLUMNS + 7u) / 8u];
static bool s_page_trend_has_data[2];
static float s_page_trend_minimum[2];
static float s_page_trend_maximum[2];
static bool s_trend_full_repaint;
static main_display_frame_t s_frame;
static uint32_t s_text_refresh_tick;
static uint32_t s_trend_refresh_tick;
static uint32_t s_display_due_tick;
static uint32_t s_perf_frame_start_tick;
static uint32_t s_perf_last_frame_ms;
static uint32_t s_perf_max_frame_ms;
static uint32_t s_perf_window_tick;
static uint16_t s_perf_window_frames;
static uint16_t s_perf_fps;
static uint32_t s_perf_sample_count;
static uint32_t s_perf_sample_missed;

static void perf_u32(char *out, uint32_t value, uint8_t digits)
{
    out[digits] = '\0';
    while (digits > 0u)
    {
        out[--digits] = (char)('0' + (value % 10u));
        value /= 10u;
    }
}

static void perf_send_u32(uint32_t value)
{
    char text[11];
    uint8_t first = 0u;
    uint8_t i;

    do
    {
        text[first++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && first < sizeof(text));
    for (i = first; i > 0u; i--)
        hal_uart_send(&((uint8_t *)text)[i - 1u], 1u);
}

static void perf_format_display(char *out)
{
    char fps[6];
    char frame_ms[6];
    uint32_t fps_value = s_perf_fps > 99u ? 99u : s_perf_fps;
    uint32_t frame_value = s_perf_last_frame_ms > 999u ? 999u :
                           s_perf_last_frame_ms;

    memcpy(out, "FPS:", 4u);
    perf_u32(fps, fps_value, 2u);
    memcpy(out + 4u, fps, 2u);
    memcpy(out + 6u, " T:", 3u);
    perf_u32(frame_ms, frame_value, 3u);
    memcpy(out + 9u, frame_ms, 3u);
    out[12] = '\0';
}

static uint16_t s_rif_bte_hits;
static uint16_t s_rif_bte_misses;

static void perf_record_frame(void)
{
    uint32_t now = HAL_GetTick();

    s_perf_last_frame_ms = now - s_perf_frame_start_tick;
    if (s_perf_last_frame_ms > s_perf_max_frame_ms)
        s_perf_max_frame_ms = s_perf_last_frame_ms;
    s_perf_window_frames++;
    if ((uint32_t)(now - s_perf_window_tick) >= 1000u)
    {
        s_perf_fps = s_perf_window_frames;
        s_perf_window_frames = 0u;
        s_perf_window_tick = now;
        hal_uart_send_text("PERF fps=");
        perf_send_u32(s_perf_fps);
        hal_uart_send_text(" frame-ms=");
        perf_send_u32(s_perf_last_frame_ms);
        hal_uart_send_text(" max-ms=");
        perf_send_u32(s_perf_max_frame_ms);
        hal_uart_send_text(" samples=");
        perf_send_u32(s_perf_sample_count);
        hal_uart_send_text(" missed=");
        perf_send_u32(s_perf_sample_missed);
        hal_uart_send_text(" bte-hit=");
        perf_send_u32(s_rif_bte_hits);
        hal_uart_send_text(" bte-miss=");
        perf_send_u32(s_rif_bte_misses);
        hal_uart_send_text("\r\n");
    }
}
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
    uint8_t mode; /* 0 = text, 2 = half */
    bool active;
} bitmap_job_t;

static bitmap_job_t s_bitmap_job;

typedef struct
{
    const char *text;
    uint16_t x;
    uint16_t y;
    uint16_t cx;
    uint16_t row;
    uint16_t column;
    uint16_t chunk_width;
    uint16_t pixel_base;
    uint16_t block_row;
    uint8_t block_rows;
    uint16_t directory_index;
    uint32_t kind;
    uint16_t code;
    uint8_t advance;
    uint8_t pixel;
    bool resolving;
    bool chunk_ready;
    bool active;
    rif_tile_t tile;
    uint8_t entry[RIF_READER_ENTRY_SIZE];
    /* Eight 64-pixel RGB565 rows. Keeping the read contiguous avoids
     * reinitializing the LT7680 Flash Master for every 32-pixel half-row. */
    uint8_t pixels[1024];
} rif_draw_job_t;

static rif_image_t s_rif_image;
static rif_draw_job_t s_rif_draw_job;
static bool s_rif_ready;
static bool s_rif_dma_probe_passed;

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
    {"mVDC", 3u, 5u, 10000u, 100000u, 0x30u, 0x04u},
    {"mVAC", 3u, 5u, 10000u, 100000u, 0x10u, 0x01u},
    {"mADC", 2u, 5u, 10000u, 200000u, 0x50u, 0x04u},
    {"mAAC", 2u, 5u, 10000u, 200000u, 0x30u, 0x02u},
    {"OHM", 4u, 4u, 1000u, 2000000u, 0x10u, 0x02u},
    {"kOHM", 3u, 4u, 1000u, 1000000u, 0x50u, 0x01u},
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
#define DEMO_SAMPLES_PER_UNIT 40u

static uint32_t s_demo_last_tick;
static uint32_t s_demo_sample;
static bool s_demo_event_reported;
static bool s_demo_commit_reported;

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
    out[0] = '+'; /* demo mantissas are always positive; keep the sign visible */
    demo_u32_to_padded(out + 1u, mant / div, u->int_digits);
    out[1u + u->int_digits] = '.';
    demo_u32_to_padded(out + u->int_digits + 2u, mant % div, u->frac_digits);
    out[u->int_digits + u->frac_digits + 2u] = '\0';
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
    uint8_t budget = 8u;

    if (!s_display_enabled)
        return;

    while ((uint32_t)(now - s_demo_last_tick) >= DEMO_SAMPLE_PERIOD_MS &&
           budget-- > 0u)
    {
        const demo_unit_t *u;
        char text[16];
        const char *p;
        uint8_t unit_index;

        s_demo_last_tick += DEMO_SAMPLE_PERIOD_MS;
        unit_index = (uint8_t)((s_demo_sample / DEMO_SAMPLES_PER_UNIT) %
                               DEMO_UNIT_COUNT);
        u = &s_demo_units[unit_index];
        demo_format_value(u, text);

        /* 0x0D start, 0x01 field tag, value+unit text, then status tags. */
        k2000_proto_feed(0x0Du);
        k2000_proto_feed(0x01u);
        for (p = text; *p != '\0'; p++)
            k2000_proto_feed((uint8_t)*p);
        demo_feed_unit(u->unit);
        k2000_proto_feed(K2000_TAG_STATUS_REL);
        k2000_proto_feed(u->status09);
        k2000_proto_feed(K2000_TAG_STATUS_HOLD);
        k2000_proto_feed(u->status08);
        k2000_proto_feed(K2000_TAG_STATUS_SHIFT);
        k2000_proto_feed((unit_index % 4u == 3u) ? 0x20u : 0x00u);
        s_demo_sample++;
        s_perf_sample_count++;
    }
    if ((uint32_t)(now - s_demo_last_tick) >= DEMO_SAMPLE_PERIOD_MS)
    {
        s_perf_sample_missed +=
            (uint32_t)(now - s_demo_last_tick) / DEMO_SAMPLE_PERIOD_MS;
        s_demo_last_tick = now;
    }
}
#endif /* K2000_DEMO_FEED */

static void display_enable_after_initial_frame(void)
{
    bool initial_complete = render_scheduler_take_initial_complete(&s_renderer);
    bool initial_frame = s_initial_page_pending;

    if ((initial_frame && initial_complete) ||
        (s_frame_rendering && s_renderer.phase == RENDER_PHASE_IDLE))
    {
        /* Re-submit the completed page even when it is numerically equal to
         * the software-visible page. The controller may retain the previous
         * MISA page after CVSSA is changed during boot. */
        if (lt7680_gfx_present_page(s_render_page) != LT7680_OK)
            return;
        if (!initial_frame && !s_demo_commit_reported)
        {
            hal_uart_send_text("[DEMO] commit page=");
            hal_uart_send_hex8(s_render_page);
            hal_uart_send_text("\r\n");
            s_demo_commit_reported = true;
        }
        s_visible_page = s_render_page;
        s_ready_page_mask |= (uint8_t)(1u << s_render_page);
        s_page_text_generation[s_render_page] = s_frame_text_generation;
        if (s_frame_has_trend_update)
        {
            s_page_trend_has_data[s_render_page] = s_frame.trend_has_data;
            s_page_trend_minimum[s_render_page] = s_frame.trend_minimum;
            s_page_trend_maximum[s_render_page] = s_frame.trend_maximum;
        }
        /* Keep the panel blank while GE completes the frame. Enabling scan only
         * after the last draw avoids exposing an in-progress SDRAM frame. */
        if (lt7680_write_reg(0x12u, 0x48u) != LT7680_OK)
            return;
        s_frame_rendering = false;
        if (!initial_frame)
            perf_record_frame();
        if (initial_frame)
        {
            s_initial_page_pending = false;
            s_display_enabled = true;
            /* The initial renderer has just been committed. Its phase must
             * not be reused by the first runtime Demo frame. */
            s_renderer.phase = RENDER_PHASE_IDLE;
            s_renderer.pending_regions = 0u;
            s_renderer.trend_pending = false;
            s_renderer.initial_complete = true;
            s_renderer.initial_complete_edge = false;
#if K2000_DEMO_FEED
            s_demo_last_tick = HAL_GetTick();
#endif
            if (!initial_complete)
                hal_uart_send_text("PASS frame page enabled\r\n");
            else
                hal_uart_send_text("PASS initial frame enabled\r\n");
        }
    }
}

static bool begin_hidden_frame(void)
{
    lt7680_status_t st;
    bool initial_frame = s_initial_page_pending;

    if (initial_frame)
    {
        /* Build the first frame on the visible canvas while the panel remains
         * blank. The initial MISA commit happens after all slices complete. */
        s_render_page = s_visible_page;
        st = lt7680_gfx_select_canvas_page(s_render_page);
        if (st != LT7680_OK)
            return false;
        s_render_full_page = true;
        st = lt7680_gfx_clear(MAIN_DISPLAY_COLOR_BG);
        if (st != LT7680_OK)
            return false;
        render_scheduler_init(&s_renderer);
        s_waiting_visible = false;
    }
    else
    {
        /* Keep runtime updates on the visible canvas while measuring the
         * incremental GE path. Full-page copy and redraw are too slow for the
         * 30 Hz budget on this controller. */
        s_render_page = s_visible_page;
        st = lt7680_gfx_select_canvas_page(s_render_page);
        if (st != LT7680_OK)
            return false;
        s_render_full_page = false;
        s_waiting_visible = false;
        s_perf_frame_start_tick = HAL_GetTick();
    }
    s_frame_rendering = true;
    s_frame_has_trend_update = s_render_full_page;
    s_render_item = 0u;
    s_render_column = 0u;
    return true;
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
        if (!s_demo_event_reported)
        {
            hal_uart_send_text("[DEMO] field-len=");
            hal_uart_send_hex8(evt->field.value_len);
            hal_uart_send_text("\r\n");
            s_demo_event_reported = true;
        }
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

/* Draw at most twelve horizontal bitmap runs. A run maps to one transformed
 * GE rectangle, bounding every call independently of string/glyph size. */
static bool ui_draw_bitmap_slice(uint16_t x, uint16_t y, const char *text,
                                 uint16_t color, uint8_t mode)
{
    /* A GE rectangle is a blocking SPI transaction.  Keep this small enough
     * that the main loop can return to the RX ISR/keypad between calls; a
     * large glyph may therefore span several cooperative calls. */
    uint16_t budget = 64u;
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

static bool rif_text_code(const char *text, uint32_t *kind, uint16_t *code,
                          uint8_t *advance)
{
    if ((uint8_t)text[0] == 0xC2u && (uint8_t)text[1] == 0xB5u)
    {
        *advance = 2u;
        return font_digit_rif_symbol_code(FONT_DIGIT_SYM_MICRO, kind, code);
    }
    if ((uint8_t)text[0] == 0xC2u && (uint8_t)text[1] == 0xB0u)
    {
        *advance = 2u;
        return font_digit_rif_symbol_code(FONT_DIGIT_SYM_DEGREE, kind, code);
    }
    if ((uint8_t)text[0] == 0xCEu && (uint8_t)text[1] == 0xA9u)
    {
        *advance = 2u;
        return font_digit_rif_symbol_code(FONT_DIGIT_SYM_OHM, kind, code);
    }
    *advance = 1u;
    return font_digit_rif_code(*text, kind, code);
}

static void rif_draw_fail(lt7680_status_t st)
{
    s_rif_draw_job.active = false;
    s_rif_ready = false;
    hal_uart_send_text("RIF fallback=");
    hal_uart_send_hex8((uint8_t)st);
    hal_uart_send_text("\r\n");
}

static bool rif_find_next_tile(void)
{
    rif_entry_t entry;
    lt7680_status_t st;

    if (s_rif_draw_job.directory_index >= s_rif_image.directory_count)
    {
        rif_draw_fail(LT7680_ERR_PARAM);
        return false;
    }
    st = lt7680_flash_read(s_rif_image.flash_base + s_rif_image.directory_offset +
                               (uint32_t)s_rif_draw_job.directory_index * RIF_READER_ENTRY_SIZE,
                           s_rif_draw_job.entry, RIF_READER_ENTRY_SIZE);
    if (st != LT7680_OK ||
        rif_reader_parse_entry(&s_rif_image, s_rif_draw_job.entry,
                               RIF_READER_ENTRY_SIZE, &entry) != RIF_OK)
    {
        rif_draw_fail(st);
        return false;
    }
    s_rif_draw_job.directory_index++;
    if (rif_reader_find_glyph(&s_rif_image, &entry, s_rif_draw_job.kind,
                              s_rif_draw_job.code, &s_rif_draw_job.tile) == RIF_OK)
    {
        s_rif_draw_job.resolving = false;
        s_rif_draw_job.row = 0u;
        s_rif_draw_job.column = 0u;
    }
    return false;
}

static bool ui_draw_external_digits(uint16_t x, uint16_t y, const char *text,
                                    uint16_t color)
{
    lt7680_status_t st;
    uint16_t budget = 8u;

#if !RIF_BTE_RENDERER
    (void)color;
#endif

    if (!s_rif_draw_job.active)
    {
        s_rif_draw_job.text = text;
        s_rif_draw_job.x = x;
        s_rif_draw_job.y = y;
        s_rif_draw_job.cx = x;
        s_rif_draw_job.active = true;
        s_rif_draw_job.resolving = true;
        s_rif_draw_job.directory_index = 0u;
        if (!rif_text_code(text, &s_rif_draw_job.kind, &s_rif_draw_job.code,
                           &s_rif_draw_job.advance))
        {
            rif_draw_fail(LT7680_ERR_PARAM);
            return false;
        }
    }
    if (s_rif_draw_job.resolving)
        return rif_find_next_tile();

#if RIF_BTE_RENDERER
    /* The cache stores the pure-transposed large glyph in framebuffer space.
     * Any unverified color, size, cache, or BTE result uses the renderer below. */
    if (color == MAIN_DISPLAY_COLOR_GREEN &&
        s_rif_draw_job.tile.foreground == MAIN_DISPLAY_COLOR_GREEN &&
        s_rif_draw_job.tile.background == MAIN_DISPLAY_COLOR_BG &&
        s_rif_draw_job.tile.width == 64u &&
        s_rif_draw_job.tile.height == 128u &&
        s_rif_draw_job.tile.stride == 128u)
    {
        rif_tile_cache_entry_t entry;
        uint16_t fb_x;
        uint16_t fb_y;

        st = rif_tile_cache_lookup(s_rif_draw_job.kind, s_rif_draw_job.code,
                                   &entry);
        if (st == LT7680_OK && entry.ready != 0u && entry.width == 128u &&
            entry.height == 64u && entry.stride >= entry.width)
        {
            panel_transform_ui_to_fb(s_rif_draw_job.cx, s_rif_draw_job.y,
                                     &fb_x, &fb_y);
            if ((uint32_t)fb_x + entry.width <= MAIN_DISPLAY_UI_HEIGHT &&
                (uint32_t)fb_y + entry.height <= MAIN_DISPLAY_UI_WIDTH)
            {
                st = lt7680_gfx_blit(s_render_page, entry.address, entry.stride,
                                     fb_x, fb_y, entry.width, entry.height);
                if (st == LT7680_OK)
                {
                    s_rif_bte_hits++;
                    s_rif_draw_job.cx = (uint16_t)(s_rif_draw_job.cx +
                                                   s_rif_draw_job.tile.width);
                    s_rif_draw_job.text += s_rif_draw_job.advance;
                    if (*s_rif_draw_job.text == '\0')
                    {
                        s_rif_draw_job.active = false;
                        return true;
                    }
                    if (!rif_text_code(s_rif_draw_job.text,
                                       &s_rif_draw_job.kind,
                                       &s_rif_draw_job.code,
                                       &s_rif_draw_job.advance))
                    {
                        rif_draw_fail(LT7680_ERR_PARAM);
                        return false;
                    }
                    s_rif_draw_job.resolving = true;
                    s_rif_draw_job.directory_index = 0u;
                    return false;
                }
            }
        }
        s_rif_bte_misses++;
    }
#endif

    if (!s_rif_draw_job.chunk_ready)
    {
        uint16_t remaining;
        if (s_rif_draw_job.block_rows == 0u)
        {
            uint16_t rows = (uint16_t)(s_rif_draw_job.tile.height -
                                       s_rif_draw_job.row);
            s_rif_draw_job.block_row = s_rif_draw_job.row;
            s_rif_draw_job.block_rows = (uint8_t)(rows > 8u ? 8u : rows);
            st = lt7680_flash_read(
                s_rif_draw_job.tile.offset +
                    (uint32_t)s_rif_draw_job.block_row * s_rif_draw_job.tile.stride,
                s_rif_draw_job.pixels,
                (uint16_t)s_rif_draw_job.block_rows * s_rif_draw_job.tile.stride);
            if (st != LT7680_OK)
            {
                rif_draw_fail(st);
                return false;
            }
            s_rif_draw_job.pixel_base = 0u;
        }
        remaining = (uint16_t)(s_rif_draw_job.tile.width -
                               s_rif_draw_job.column);
        s_rif_draw_job.chunk_width = remaining;
        s_rif_draw_job.pixel = 0u;
        s_rif_draw_job.chunk_ready = true;
    }
    while (s_rif_draw_job.pixel < s_rif_draw_job.chunk_width && budget > 0u)
    {
        uint8_t start;
        uint16_t color;
        while (s_rif_draw_job.pixel < s_rif_draw_job.chunk_width &&
               ((uint16_t)s_rif_draw_job.pixels[s_rif_draw_job.pixel_base +
                                                s_rif_draw_job.pixel * 2u] |
                ((uint16_t)s_rif_draw_job.pixels[s_rif_draw_job.pixel_base +
                                                s_rif_draw_job.pixel * 2u + 1u] << 8)) ==
                   s_rif_draw_job.tile.background)
            s_rif_draw_job.pixel++;
        if (s_rif_draw_job.pixel == s_rif_draw_job.chunk_width)
            break;
        start = s_rif_draw_job.pixel;
        color = (uint16_t)s_rif_draw_job.pixels[s_rif_draw_job.pixel_base +
                                                start * 2u] |
                ((uint16_t)s_rif_draw_job.pixels[s_rif_draw_job.pixel_base +
                                                start * 2u + 1u] << 8);
        while (s_rif_draw_job.pixel < s_rif_draw_job.chunk_width &&
               ((uint16_t)s_rif_draw_job.pixels[s_rif_draw_job.pixel_base +
                                                s_rif_draw_job.pixel * 2u] |
                ((uint16_t)s_rif_draw_job.pixels[s_rif_draw_job.pixel_base +
                                                s_rif_draw_job.pixel * 2u + 1u] << 8)) == color)
            s_rif_draw_job.pixel++;
        if (ui_fill_rect((uint16_t)(s_rif_draw_job.cx + s_rif_draw_job.column + start),
                         (uint16_t)(s_rif_draw_job.y + s_rif_draw_job.row),
                         (uint16_t)(s_rif_draw_job.pixel - start), 1u, color) != LT7680_OK)
        {
            rif_draw_fail(LT7680_ERR_BUS);
            return false;
        }
        budget--;
    }
    if (s_rif_draw_job.pixel < s_rif_draw_job.chunk_width)
        return false;

    s_rif_draw_job.chunk_ready = false;
    s_rif_draw_job.column = (uint16_t)(s_rif_draw_job.column + s_rif_draw_job.chunk_width);
    if (s_rif_draw_job.column < s_rif_draw_job.tile.width)
        return false;
    s_rif_draw_job.column = 0u;
    s_rif_draw_job.row++;
    s_rif_draw_job.pixel_base = (uint16_t)(s_rif_draw_job.pixel_base +
                                           s_rif_draw_job.tile.stride);
    if (s_rif_draw_job.row >= (uint16_t)(s_rif_draw_job.block_row +
                                         s_rif_draw_job.block_rows))
    {
        s_rif_draw_job.block_rows = 0u;
    }
    if (s_rif_draw_job.row < s_rif_draw_job.tile.height)
        return false;

    s_rif_draw_job.cx = (uint16_t)(s_rif_draw_job.cx + s_rif_draw_job.tile.width);
    s_rif_draw_job.text += s_rif_draw_job.advance;
    if (*s_rif_draw_job.text == '\0')
    {
        s_rif_draw_job.active = false;
        return true;
    }
    if (!rif_text_code(s_rif_draw_job.text, &s_rif_draw_job.kind,
                       &s_rif_draw_job.code, &s_rif_draw_job.advance))
    {
        rif_draw_fail(LT7680_ERR_PARAM);
        return false;
    }
    s_rif_draw_job.resolving = true;
    s_rif_draw_job.directory_index = 0u;
    return false;
}

static bool ui_draw_digits(uint16_t x, uint16_t y, const char *text,
                           uint16_t color)
{
    return s_rif_ready ? ui_draw_external_digits(x, y, text, color)
                       : ui_draw_text(x, y, text, color);
}

static void __attribute__((unused)) rif_log_spi_registers(const char *phase)
{
    static const struct
    {
        uint8_t address;
        const char *name;
    } registers[] = {
        {0xB7u, "SFL_CTRL"},
        {0xB9u, "SPIMCR2"},
        {0xBAu, "SPIMSR"},
        {0xBBu, "DIVISOR"},
        {0x01u, "HOST_IF/CCR"},
    };
    uint8_t i;

    hal_uart_send_text("RIF LT7680 SPI ");
    hal_uart_send_text(phase);
    for (i = 0u; i < (uint8_t)(sizeof(registers) / sizeof(registers[0])); i++)
    {
        uint8_t value;

        hal_uart_send_text(" ");
        hal_uart_send_text(registers[i].name);
        hal_uart_send_text("=0x");
        if (lt7680_read_reg(registers[i].address, &value) == LT7680_OK)
        {
            hal_uart_send_hex8(value);
        }
        else
        {
            hal_uart_send_text("ERR");
        }
    }
    hal_uart_send_text("\r\n");
}

static void rif_probe_send_hex32(uint32_t value)
{
    hal_uart_send_hex8((uint8_t)(value >> 24));
    hal_uart_send_hex8((uint8_t)(value >> 16));
    hal_uart_send_hex8((uint8_t)(value >> 8));
    hal_uart_send_hex8((uint8_t)value);
}

static bool rif_find_tile_char(uint16_t code, rif_tile_t *tile)
{
    rif_entry_t entry;
    uint8_t entry_data[RIF_READER_ENTRY_SIZE];
    uint16_t i;

    for (i = 0u; i < s_rif_image.directory_count; i++)
    {
        if (lt7680_flash_read(s_rif_image.flash_base +
                              s_rif_image.directory_offset +
                              (uint32_t)i * RIF_READER_ENTRY_SIZE,
                              entry_data, RIF_READER_ENTRY_SIZE) != LT7680_OK ||
            rif_reader_parse_entry(&s_rif_image, entry_data,
                                   RIF_READER_ENTRY_SIZE, &entry) != RIF_OK)
            return false;
        if (rif_reader_find_glyph(&s_rif_image, &entry, RIF_KIND_DIGIT_CHAR,
                                  code, tile) == RIF_OK)
            return true;
    }
    return false;
}

static void rif_dma_snapshot_send(const char *label,
                                  uint16_t probe_h,
                                  uint32_t source, uint32_t target,
                                  const lt7680_flash_dma_snapshot_t *before,
                                  const lt7680_flash_dma_snapshot_t *after,
                                  lt7680_status_t transfer_status)
{
    uint8_t i;

    hal_uart_send_text("RIF DMA regs before=");
    hal_uart_send_hex8(before->b6);
    hal_uart_send_hex8(before->b7);
    hal_uart_send_hex8(before->b9);
    hal_uart_send_hex8(before->ba);
    hal_uart_send_hex8(before->bb);
    for (i = 0u; i < sizeof(before->bc_cb); i++)
        hal_uart_send_hex8(before->bc_cb[i]);
    hal_uart_send_text(" after=");
    hal_uart_send_hex8(after->b6);
    hal_uart_send_hex8(after->b7);
    hal_uart_send_hex8(after->b9);
    hal_uart_send_hex8(after->ba);
    hal_uart_send_hex8(after->bb);
    for (i = 0u; i < sizeof(after->bc_cb); i++)
        hal_uart_send_hex8(after->bc_cb[i]);
    hal_uart_send_text(" status=");
    hal_uart_send_hex8((uint8_t)transfer_status);
    hal_uart_send_text(" height=");
    hal_uart_send_hex8((uint8_t)(probe_h >> 8));
    hal_uart_send_hex8((uint8_t)probe_h);
    hal_uart_send_text(" source=");
    rif_probe_send_hex32(source);
    hal_uart_send_text(" target=");
    rif_probe_send_hex32(target);
    hal_uart_send_text(" target-page=");
    hal_uart_send_hex8((uint8_t)(target >> 20));
    hal_uart_send_text(" cvssa=");
    rif_probe_send_hex32(after->cvssa);
    hal_uart_send_text(" stride=");
    hal_uart_send_hex8((uint8_t)(after->canvas_stride >> 8));
    hal_uart_send_hex8((uint8_t)after->canvas_stride);
    hal_uart_send_text(" dma_status=");
    hal_uart_send_hex8(after->b6);
    hal_uart_send_text(" core=");
    hal_uart_send_hex8(after->core_status);
    hal_uart_send_text(" sdram=");
    hal_uart_send_hex8(after->sdram_status);
    hal_uart_send_text(" sample0=UNSUPPORTED sample1=UNSUPPORTED method=");
    hal_uart_send_text(label);
    hal_uart_send_text("\r\n");
}

static void rif_dma_probe(void)
{
    static const uint32_t staging_addr = 0x200000u;
    static const uint16_t probe_x = 16u;
    static const uint16_t probe_y = 16u;
    static const uint16_t probe_w = 64u;
    static const uint16_t probe_heights[] = {20u};
    rif_tile_t tile = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
    lt7680_flash_dma_snapshot_t before;
    lt7680_flash_dma_snapshot_t after;
    lt7680_status_t dma_status = LT7680_ERR_UNSUPPORTED;
    lt7680_status_t bte_status = LT7680_ERR_UNSUPPORTED;
    lt7680_status_t display_status = LT7680_ERR_UNSUPPORTED;
    lt7680_status_t restore_status = LT7680_OK;
    uint16_t first_success_h = 0u;
    uint16_t last_probe_h = 0u;
    uint16_t reported_h = 0u;
    bool visual_ready = false;

    /* DMA probes several crop heights to find the LT7680 block-geometry limit.
     * The staging address is outside both canvas pages; the current API cannot
     * target a hidden page directly. */
    if (rif_find_tile_char((uint16_t)'8', &tile) &&
        tile.width == 64u && tile.height == 128u &&
        tile.stride == 128u && tile.size == 16384u)
    {
        for (uint8_t i = 0u; i < (uint8_t)(sizeof(probe_heights) /
                                           sizeof(probe_heights[0])); ++i)
        {
            uint16_t probe_h = probe_heights[i];
            last_probe_h = probe_h;

            (void)lt7680_flash_dma_read_snapshot(&before);
            dma_status = lt7680_flash_dma_to_sdram(tile.offset, staging_addr,
                                                    (uint16_t)(probe_w * 2u),
                                                    probe_h, probe_w);
            (void)lt7680_flash_dma_read_snapshot(&after);
            rif_dma_snapshot_send("tile8-visual", probe_h, tile.offset,
                                  staging_addr, &before, &after, dma_status);
            if (dma_status == LT7680_OK)
            {
                if (first_success_h == 0u)
                    first_success_h = probe_h;
            }
        }
    }

    if (first_success_h != 0u)
    {
        bte_status = lt7680_gfx_blit(1u, staging_addr, probe_w, probe_x,
                                     probe_y, probe_w, first_success_h);
        if (bte_status == LT7680_OK)
        {
            display_status = lt7680_gfx_present_page(1u);
            if (display_status == LT7680_OK)
            {
                display_status = lt7680_write_reg(0x12u, 0x48u);
                visual_ready = display_status == LT7680_OK;
            }
        }
    }

    reported_h = first_success_h != 0u ? first_success_h : last_probe_h;

    hal_uart_send_text("RIF DMA visual status=");
    hal_uart_send_hex8((uint8_t)(visual_ready ? LT7680_OK :
                                 (dma_status != LT7680_OK ? dma_status :
                                  (bte_status != LT7680_OK ? bte_status :
                                   display_status))));
    hal_uart_send_text(" source=");
    rif_probe_send_hex32(tile.offset);
    hal_uart_send_text(" target-page=1 target-rect=");
    hal_uart_send_hex8((uint8_t)(probe_x >> 8));
    hal_uart_send_hex8((uint8_t)probe_x);
    hal_uart_send_text(",");
    hal_uart_send_hex8((uint8_t)(probe_y >> 8));
    hal_uart_send_hex8((uint8_t)probe_y);
    hal_uart_send_text("+");
    hal_uart_send_hex8((uint8_t)(probe_w >> 8));
    hal_uart_send_hex8((uint8_t)probe_w);
    hal_uart_send_text("x");
    hal_uart_send_hex8((uint8_t)(reported_h >> 8));
    hal_uart_send_hex8((uint8_t)reported_h);
    hal_uart_send_text(" direct-hidden=UNSUPPORTED");
    if (visual_ready)
    {
        hal_uart_send_text(" visual=DISPLAYED-ONCE");
        (void)lt7680_delay_ms(250u);
    }
    else
    {
        hal_uart_send_text(" visual=NOT-DISPLAYED");
    }
    hal_uart_send_text("\r\n");
    /* Do not leave diagnostic pixels or page identity visible to the normal
     * boot renderer, regardless of which probe step failed. */
    if (lt7680_write_reg(0x12u, 0x08u) != LT7680_OK)
        restore_status = LT7680_ERR_BUS;
    if (lt7680_gfx_select_canvas_page(1u) != LT7680_OK ||
        lt7680_gfx_clear(0x0000u) != LT7680_OK)
        restore_status = LT7680_ERR_BUS;
    if (lt7680_gfx_select_canvas_page(0u) != LT7680_OK ||
        lt7680_gfx_present_page(0u) != LT7680_OK)
        restore_status = LT7680_ERR_BUS;
    if (restore_status != LT7680_OK)
        hal_uart_send_text("RIF DMA visual restore=FAIL\r\n");
    else
        hal_uart_send_text("RIF DMA visual restore=BLACK-PAGE-0\r\n");
}

static bool rif_cache_pixel_probe(void)
{
    static const uint32_t cache_base = 0x300000u;
    static const uint16_t pattern[] = {0x07E0u, 0xF800u, 0x001Fu, 0xFFFFu};
    lt7680_flash_dma_snapshot_t saved;
    uint16_t pixel;
    uint8_t i;
    bool ok = true;

    if (lt7680_flash_dma_read_snapshot(&saved) != LT7680_OK ||
        lt7680_gfx_set_canvas_base(cache_base) != LT7680_OK ||
        lt7680_gfx_set_canvas_width(128u) != LT7680_OK)
        return false;

    for (i = 0u; i < (uint8_t)(sizeof(pattern) / sizeof(pattern[0])); i++) {
        if (lt7680_gfx_write_pixels((uint16_t)(i * 4u), 0u, &pattern[i], 1u) !=
            LT7680_OK ||
            lt7680_gfx_peek_pixel((uint16_t)(i * 4u), 0u, &pixel) != LT7680_OK ||
            pixel != pattern[i]) {
            ok = false;
            hal_uart_send_text("RIF cache pixel mismatch index=");
            hal_uart_send_hex8(i);
            hal_uart_send_text(" expected=");
            hal_uart_send_hex8((uint8_t)(pattern[i] >> 8));
            hal_uart_send_hex8((uint8_t)pattern[i]);
            hal_uart_send_text(" actual=");
            hal_uart_send_hex8((uint8_t)(pixel >> 8));
            hal_uart_send_hex8((uint8_t)pixel);
            hal_uart_send_text("\r\n");
            break;
        }
    }

    if (lt7680_gfx_set_canvas_base(saved.cvssa) != LT7680_OK ||
        lt7680_gfx_set_canvas_width(saved.canvas_stride) != LT7680_OK)
        ok = false;

    hal_uart_send_text("RIF cache pixel probe=");
    hal_uart_send_text(ok ? "PASS\r\n" : "FAIL\r\n");
    hal_uart_send_text("RIF cache canvas restore cvssa=");
    hal_uart_send_hex8((uint8_t)(saved.cvssa >> 24));
    hal_uart_send_hex8((uint8_t)(saved.cvssa >> 16));
    hal_uart_send_hex8((uint8_t)(saved.cvssa >> 8));
    hal_uart_send_hex8((uint8_t)saved.cvssa);
    hal_uart_send_text(" stride=");
    hal_uart_send_hex8((uint8_t)(saved.canvas_stride >> 8));
    hal_uart_send_hex8((uint8_t)saved.canvas_stride);
    hal_uart_send_text("\r\n");
    return ok;
}

static void rif_log_bte_snapshot(void)
{
    lt7680_bte_snapshot_t snapshot;

    if (lt7680_gfx_get_last_bte_setup(&snapshot) != LT7680_OK) {
        hal_uart_send_text("RIF BTE snapshot=ERROR\r\n");
        return;
    }
    hal_uart_send_text("RIF BTE snapshot ctrl0=");
    hal_uart_send_hex8(snapshot.ctrl0);
    hal_uart_send_text(" ctrl1=");
    hal_uart_send_hex8(snapshot.ctrl1);
    hal_uart_send_text(" colr=");
    hal_uart_send_hex8(snapshot.colr);
    hal_uart_send_text(" saddr=");
    rif_probe_send_hex32(snapshot.source_address);
    hal_uart_send_text(" swth=");
    hal_uart_send_hex8((uint8_t)(snapshot.source_width >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.source_width);
    hal_uart_send_text(" sxy=");
    hal_uart_send_hex8((uint8_t)(snapshot.source_x >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.source_x);
    hal_uart_send_hex8((uint8_t)(snapshot.source_y >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.source_y);
    hal_uart_send_text(" daddr=");
    rif_probe_send_hex32(snapshot.destination_address);
    hal_uart_send_text(" dwth=");
    hal_uart_send_hex8((uint8_t)(snapshot.destination_width >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.destination_width);
    hal_uart_send_text(" dxy=");
    hal_uart_send_hex8((uint8_t)(snapshot.destination_x >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.destination_x);
    hal_uart_send_hex8((uint8_t)(snapshot.destination_y >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.destination_y);
    hal_uart_send_text(" size=");
    hal_uart_send_hex8((uint8_t)(snapshot.width >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.width);
    hal_uart_send_hex8((uint8_t)(snapshot.height >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.height);
    hal_uart_send_text("\r\n");
}

/* Command-level probe: blit a 4x4 block from the off-screen cache base to a
 * hidden page-1 location without switching the visible page, then log the
 * captured BTE setup so the cache-path parameters can be verified against
 * saddr=00300000 swth=0080 size=00040004. */
static void rif_cache_bte_probe(void)
{
    lt7680_bte_snapshot_t snapshot;
    lt7680_flash_dma_snapshot_t saved;
    uint16_t line[4] = {0x07E0u, 0x07E0u, 0x07E0u, 0x07E0u};
    lt7680_status_t st;
    uint8_t row;

    /* Fill a full 4x4 solid-green block at cache (0,0) first; the pixel
     * probe only writes isolated pixels, and an unwritten source area would
     * show stale SDRAM content as garbled colors. */
    if (lt7680_flash_dma_read_snapshot(&saved) != LT7680_OK ||
        lt7680_gfx_set_canvas_base(0x300000u) != LT7680_OK ||
        lt7680_gfx_set_canvas_width(128u) != LT7680_OK)
        return;
    for (row = 0u; row < 4u; row++)
        (void)lt7680_gfx_write_pixels(0u, row, line, 4u);
    if (lt7680_gfx_set_canvas_base(saved.cvssa) != LT7680_OK ||
        lt7680_gfx_set_canvas_width(saved.canvas_stride) != LT7680_OK)
        return;

    st = lt7680_gfx_blit(1u, 0x300000u, 128u, 200u, 500u, 4u, 4u);
    if (st != LT7680_OK) {
        hal_uart_send_text("RIF cache BTE probe status=");
        hal_uart_send_hex8((uint8_t)st);
        hal_uart_send_text("\r\n");
        return;
    }
    if (lt7680_gfx_get_last_bte_setup(&snapshot) != LT7680_OK) {
        hal_uart_send_text("RIF cache BTE probe snapshot=ERROR\r\n");
        return;
    }
    hal_uart_send_text("RIF cache BTE probe ctrl1=");
    hal_uart_send_hex8(snapshot.ctrl1);
    hal_uart_send_text(" colr=");
    hal_uart_send_hex8(snapshot.colr);
    hal_uart_send_text(" saddr=");
    rif_probe_send_hex32(snapshot.source_address);
    hal_uart_send_text(" swth=");
    hal_uart_send_hex8((uint8_t)(snapshot.source_width >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.source_width);
    hal_uart_send_text(" daddr=");
    rif_probe_send_hex32(snapshot.destination_address);
    hal_uart_send_text(" dwth=");
    hal_uart_send_hex8((uint8_t)(snapshot.destination_width >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.destination_width);
    hal_uart_send_text(" dxy=");
    hal_uart_send_hex8((uint8_t)(snapshot.destination_x >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.destination_x);
    hal_uart_send_hex8((uint8_t)(snapshot.destination_y >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.destination_y);
    hal_uart_send_text(" size=");
    hal_uart_send_hex8((uint8_t)(snapshot.width >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.width);
    hal_uart_send_hex8((uint8_t)(snapshot.height >> 8));
    hal_uart_send_hex8((uint8_t)snapshot.height);
    hal_uart_send_text("\r\n");

    /* One-shot visual acceptance: show the blitted 4x4 block once. The cache
     * pixel probe wrote pure green 0x07E0 at cache (0,0), so a correct BTE
     * copy shows a small solid-green square; stretching or wrong colors mean
     * the cached tile content is still bad. Restore black page 0 afterwards. */
    if (lt7680_gfx_present_page(1u) == LT7680_OK &&
        lt7680_write_reg(0x12u, 0x48u) == LT7680_OK) {
        (void)lt7680_delay_ms(400u);
        hal_uart_send_text("RIF cache BTE visual=SHOWN\r\n");
    }
    if (lt7680_write_reg(0x12u, 0x08u) != LT7680_OK ||
        lt7680_gfx_select_canvas_page(1u) != LT7680_OK ||
        lt7680_gfx_clear(0x0000u) != LT7680_OK ||
        lt7680_gfx_select_canvas_page(0u) != LT7680_OK ||
        lt7680_gfx_present_page(0u) != LT7680_OK)
        hal_uart_send_text("RIF cache BTE visual restore=FAIL\r\n");
    else
        hal_uart_send_text("RIF cache BTE visual restore=BLACK-PAGE-0\r\n");
}

static void rif_init(void)
{
    uint8_t header[RIF_READER_HEADER_SIZE];
    uint8_t id[3] = {0u, 0u, 0u};
    uint8_t spi_status;
    lt7680_status_t st;
    lt7680_flash_b7_probe_t b7_probe;
    bool header_ready = false;

    {
        lt7680_flash_jedec_diag_t diag;
        lt7680_status_t diag_status = lt7680_flash_jedec_diagnostic(&diag);
        hal_uart_send_text("RIF JEDEC diag status=0x");
        hal_uart_send_hex8((uint8_t)diag_status);
        hal_uart_send_text(" batch=");
        for (uint8_t i = 0u; i < 4u; i++)
            hal_uart_send_hex8(diag.batch_raw[i]);
        hal_uart_send_text(" step=");
        for (uint8_t i = 0u; i < 4u; i++)
            hal_uart_send_hex8(diag.step_raw[i]);
        hal_uart_send_text(" batch-status=0x");
        hal_uart_send_hex8((uint8_t)diag.batch_status);
        hal_uart_send_text(" step-status=0x");
        hal_uart_send_hex8((uint8_t)diag.step_status);
        hal_uart_send_text("\r\n");
    }
    {
        lt7680_flash_cs_diag_t diag;
        lt7680_status_t diag_status = lt7680_flash_cs_diagnostic(&diag);
        hal_uart_send_text("RIF SFCS diag status=0x");
        hal_uart_send_hex8((uint8_t)diag_status);
        hal_uart_send_text(" sfcs0=");
        for (uint8_t i = 0u; i < 4u; i++)
            hal_uart_send_hex8(diag.sfcs0_raw[i]);
        hal_uart_send_text(" sfcs1=");
        for (uint8_t i = 0u; i < 4u; i++)
            hal_uart_send_hex8(diag.sfcs1_raw[i]);
        hal_uart_send_text(" status0=0x");
        hal_uart_send_hex8((uint8_t)diag.sfcs0_status);
        hal_uart_send_text(" status1=0x");
        hal_uart_send_hex8((uint8_t)diag.sfcs1_status);
        hal_uart_send_text("\r\n");
    }

    s_rif_ready = false;
    rif_tile_cache_init();
    rif_log_spi_registers("regs");
    st = lt7680_flash_read(0u, header, sizeof(header));
    {
        lt7680_flash_header_probe_t header_probe;
        uint8_t i;
        lt7680_flash_get_header_probe(&header_probe);
        hal_uart_send_text("RIF header status=0x");
        hal_uart_send_hex8((uint8_t)header_probe.status);
        hal_uart_send_text(" attempted=");
        hal_uart_send_hex8(header_probe.attempted);
        hal_uart_send_text(" raw=");
        for (i = 0u; i < sizeof(header_probe.raw); i++)
        {
            hal_uart_send_hex8(header_probe.raw[i]);
        }
        hal_uart_send_text("\r\n");
    }
    if (st == LT7680_OK &&
        rif_reader_parse_header(header, sizeof(header), &s_rif_image) == RIF_OK)
    {
        header_ready = true;
        s_rif_ready = true;
    }
    st = lt7680_flash_read_jedec_id(id);
    /* Capture the controller after the JEDEC transaction has returned. This
     * is deliberately read-only: do not touch SPIDR or clear SPIMSR flags. */
    rif_log_spi_registers("post");
    lt7680_flash_get_b7_probe(&b7_probe);
    hal_uart_send_text("RIF B7 write-probe attempted=");
    hal_uart_send_hex8(b7_probe.attempted);
    hal_uart_send_text(" requested=0x");
    hal_uart_send_hex8(b7_probe.requested);
    hal_uart_send_text(" readback=0x");
    if (b7_probe.read_status == LT7680_OK)
    {
        hal_uart_send_hex8(b7_probe.readback);
    }
    else
    {
        hal_uart_send_text("ERR");
    }
    hal_uart_send_text(" write-status=0x");
    hal_uart_send_hex8((uint8_t)b7_probe.write_status);
    hal_uart_send_text(" read-status=0x");
    hal_uart_send_hex8((uint8_t)b7_probe.read_status);
    hal_uart_send_text("\r\n");
    if (st != LT7680_OK)
    {
        hal_uart_send_text("RIF JEDEC read failed=0x");
        hal_uart_send_hex8((uint8_t)st);
        if (lt7680_read_reg(0xBAu, &spi_status) == LT7680_OK)
        {
            hal_uart_send_text(" SPIMSR=0x");
            hal_uart_send_hex8(spi_status);
        }
        hal_uart_send_text("\r\n");
    }
    else
    {
        hal_uart_send_text("RIF JEDEC=");
        hal_uart_send_hex8(id[0]);
        hal_uart_send_hex8(id[1]);
        hal_uart_send_hex8(id[2]);
        hal_uart_send_text("\r\n");
    }
    {
        lt7680_flash_jedec_probe_t jedec_probe;
        lt7680_flash_get_jedec_probe(&jedec_probe);
        hal_uart_send_text("RIF JEDEC raw=");
        hal_uart_send_hex8(jedec_probe.raw[0]);
        hal_uart_send_hex8(jedec_probe.raw[1]);
        hal_uart_send_hex8(jedec_probe.raw[2]);
        hal_uart_send_hex8(jedec_probe.raw[3]);
        hal_uart_send_text("\r\n");
    }
    {
        lt7680_flash_fifo_probe_t fifo_probe;
        lt7680_flash_get_fifo_probe(&fifo_probe);
        hal_uart_send_text("RIF FIFO probe attempted=");
        hal_uart_send_hex8(fifo_probe.attempted);
        hal_uart_send_text(" full=");
        hal_uart_send_hex8(fifo_probe.full_count);
        hal_uart_send_text(" status_err=");
        hal_uart_send_hex8(fifo_probe.status_err);
        hal_uart_send_text(" last_status=0x");
        hal_uart_send_hex8(fifo_probe.last_status);
        hal_uart_send_text("\r\n");
    }
    {
        lt7680_flash_spi_snapshot_t spi_snapshot;
        lt7680_flash_get_spi_snapshot(&spi_snapshot);
        hal_uart_send_text("RIF SPI snapshot attempted=");
        hal_uart_send_hex8(spi_snapshot.attempted);
        hal_uart_send_text(" B6=0x");
        hal_uart_send_hex8(spi_snapshot.b6);
        hal_uart_send_text(" B7=0x");
        hal_uart_send_hex8(spi_snapshot.b7);
        hal_uart_send_text(" B9=0x");
        hal_uart_send_hex8(spi_snapshot.b9);
        hal_uart_send_text(" BA=0x");
        hal_uart_send_hex8(spi_snapshot.ba);
        hal_uart_send_text(" BB=0x");
        hal_uart_send_hex8(spi_snapshot.bb);
        hal_uart_send_text(" status=0x");
        hal_uart_send_hex8((uint8_t)spi_snapshot.status);
        hal_uart_send_text("\r\n");
    }
    if (!header_ready)
    {
        hal_uart_send_text("RIF unavailable: invalid header\r\n");
        return;
    }
    s_rif_dma_probe_passed = false;
    rif_dma_probe();
    {
        bool cache_pixel_ok = rif_cache_pixel_probe();
        s_rif_dma_probe_passed = s_rif_dma_probe_passed && cache_pixel_ok;
    }
    rif_log_bte_snapshot();
    rif_cache_bte_probe();
    /* The BTE probe currently reports command completion only; until its
     * pixels are independently accepted, do not present its diagnostic page
     * during normal boot. */
    if (s_rif_dma_probe_passed)
    {
        bool cache_ready = true;
        static const char cache_chars[] =
            "0123456789.+-Ee%mukKMWVOhDAC?RFLHzs";
        uint16_t char_index;

        for (char_index = 0u; cache_chars[char_index] != '\0'; char_index++)
        {
            rif_tile_t cache_tile;
            rif_tile_cache_entry_t cache_entry;

            if (!rif_find_tile_char((uint16_t)cache_chars[char_index],
                                    &cache_tile))
            {
                cache_ready = false;
                continue;
            }
            st = rif_tile_cache_prepare(RIF_KIND_DIGIT_CHAR,
                                        (uint16_t)cache_chars[char_index],
                                        &cache_tile,
                                        &cache_entry);
            if (st != LT7680_OK || cache_entry.ready == 0u)
                cache_ready = false;
        }
        if (!cache_ready)
            hal_uart_send_text("RIF tile cache unavailable\r\n");
        else
            hal_uart_send_text("RIF digit cache ready\r\n");
    }
#if RIF_BTE_RENDERER
    hal_uart_send_text("RIF BTE renderer=");
    hal_uart_send_text(s_rif_dma_probe_passed ? "ON\r\n" : "OFF\r\n");
#else
    hal_uart_send_text("RIF BTE renderer=OFF (compile)\r\n");
#endif
    hal_uart_send_text("RIF external digits ready\r\n");
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

/* Draw one step of the right-side info panel: a 4-row rectangle of
 * Excel-style name/value cells (Zin / Range / Rate / Status) with the value
 * cell of the Status row holding the FILT REL MATH lamps. Step n maps to
 * row n/4 and action n%4 (fill name, fill value, name text, value text).
 * Returns false while a resumable bitmap draw still has work pending, so the
 * caller retries the same step. */
static bool reading_draw_info(uint8_t n)
{
    static const char *const info_names[4] = {"Zin", "Range", "Rate",
                                              "Status"};
    static const uint8_t info_ys[4] = {MAIN_DISPLAY_INFO_ZIN_Y,
                                       MAIN_DISPLAY_INFO_RANGE_Y,
                                       MAIN_DISPLAY_INFO_RATE_Y,
                                       MAIN_DISPLAY_INFO_STATUS_Y};
    static const uint16_t info_color[4] = {MAIN_DISPLAY_COLOR_MUTED,
                                           MAIN_DISPLAY_COLOR_WHITE,
                                           MAIN_DISPLAY_COLOR_WHITE,
                                           MAIN_DISPLAY_COLOR_WHITE};
    uint8_t row = (uint8_t)(n / 4u);
    uint8_t sub = (uint8_t)(n % 4u);
    uint16_t vx = (uint16_t)(MAIN_DISPLAY_INFO_X + MAIN_DISPLAY_INFO_NAME_W);
    uint16_t ty;
    if (row >= 4u)
        return true;
    ty = (uint16_t)(info_ys[row] +
                    (MAIN_DISPLAY_INFO_ROW_H - FONT_TEXT_HEIGHT) / 2u);
    switch (sub)
    {
    case 0u:
        (void)ui_fill_rect(MAIN_DISPLAY_INFO_X, info_ys[row],
                           MAIN_DISPLAY_INFO_NAME_W, MAIN_DISPLAY_INFO_ROW_H,
                           MAIN_DISPLAY_COLOR_BAR);
        return true;
    case 1u:
        (void)ui_fill_rect(vx, info_ys[row], MAIN_DISPLAY_INFO_VALUE_W,
                           MAIN_DISPLAY_INFO_ROW_H, MAIN_DISPLAY_COLOR_BAR_ALT);
        return true;
    case 2u:
        return ui_draw_text(MAIN_DISPLAY_INFO_X, ty, info_names[row],
                            info_color[row]);
    default:
        switch (row)
        {
        case 0u:
            return ui_draw_text(vx, ty, s_frame.impedance,
                                MAIN_DISPLAY_COLOR_MUTED);
        case 1u:
            return ui_draw_text(vx, ty, s_frame.range,
                                MAIN_DISPLAY_COLOR_WHITE);
        case 2u:
            return ui_draw_text(vx, ty, s_frame.rate,
                                MAIN_DISPLAY_COLOR_WHITE);
        default:
            if (!ui_draw_text(vx, ty, "FILT",
                              s_frame.status_active[7] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED))
                return false;
            if (!ui_draw_text((uint16_t)(vx + 4u * FONT_TEXT_WIDTH), ty, "REL",
                              s_frame.status_active[6] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED))
                return false;
            if (!ui_draw_text((uint16_t)(vx + 8u * FONT_TEXT_WIDTH), ty, "MATH",
                              s_frame.status_active[11] ? MAIN_DISPLAY_COLOR_GREEN : MAIN_DISPLAY_COLOR_MUTED))
                return false;
            return true;
        }
    }
}

static bool trend_drawn_occupied(uint16_t column)
{
    return (s_drawn_trend_occupied[s_render_page][column >> 3] &
            (uint8_t)(1u << (column & 7u))) != 0u;
}

static void trend_set_drawn(uint16_t column, bool occupied,
                            uint8_t y0, uint8_t y1)
{
    uint8_t mask = (uint8_t)(1u << (column & 7u));
    if (occupied)
        s_drawn_trend_occupied[s_render_page][column >> 3] |= mask;
    else
        s_drawn_trend_occupied[s_render_page][column >> 3] &= (uint8_t)~mask;
    s_drawn_trend_y0[s_render_page][column] = y0;
    s_drawn_trend_y1[s_render_page][column] = y1;
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
    bool occupied = c->occupied && s_frame.trend_has_data;

    if (occupied)
    {
        float span = s_frame.trend_maximum - s_frame.trend_minimum;
        y0 = (uint8_t)((s_frame.trend_maximum - c->maximum) *
                       MAIN_DISPLAY_PLOT_H / span);
        y1 = (uint8_t)((s_frame.trend_maximum - c->minimum) *
                       MAIN_DISPLAY_PLOT_H / span);
    }
    if (!s_trend_full_repaint &&
        occupied == trend_drawn_occupied(column) &&
        (!occupied || (s_drawn_trend_y0[s_render_page][column] == y0 &&
                       s_drawn_trend_y1[s_render_page][column] == y1)))
    {
        return;
    }

    if (erase_previous && trend_drawn_occupied(column))
    {
        uint16_t old_y0 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     s_drawn_trend_y0[s_render_page][column]);
        uint16_t old_y1 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     s_drawn_trend_y1[s_render_page][column]);
        (void)ui_fill_rect(x0, old_y0, (uint16_t)(x1 - x0 + 1u),
                           (uint16_t)(old_y1 - old_y0 + 1u),
                           MAIN_DISPLAY_COLOR_BG);
        trend_restore_grid(x0, x1, old_y0, old_y1);
    }
    if (occupied)
    {
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
    trend_set_drawn(column, occupied, y0, y1);
}

static void trend_draw_background(void)
{
    (void)ui_fill_rect(0u, MAIN_DISPLAY_CHART_PANEL_Y,
                       MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_CHART_PANEL_H,
                       MAIN_DISPLAY_COLOR_BAR);
    (void)ui_fill_rect(MAIN_DISPLAY_PLOT_X, MAIN_DISPLAY_PLOT_BG_Y,
                       MAIN_DISPLAY_PLOT_W, MAIN_DISPLAY_PLOT_BG_H,
                       MAIN_DISPLAY_COLOR_BG);
    (void)ui_fill_rect(MAIN_DISPLAY_PLOT_X, MAIN_DISPLAY_PLOT_DIVIDER_Y,
                       MAIN_DISPLAY_PLOT_W, MAIN_DISPLAY_PLOT_DIVIDER_H,
                       MAIN_DISPLAY_COLOR_BAR_ALT);
}

static uint16_t trend_x_label_x(uint16_t center, const char *label)
{
    size_t width = strlen(label) * FONT_TEXT_WIDTH;
    uint16_t x = center > width / 2u ? (uint16_t)(center - width / 2u)
                                     : MAIN_DISPLAY_PLOT_X;
    if (x + width > MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W)
        x = (uint16_t)(MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W - width);
    return x;
}

static void reading_scene_render(void)
{
    uint32_t now = HAL_GetTick();
    bool initial_phase;
    uint8_t due_regions;
    bool trend_due;
    bool display_due;
    bool trend_needed;
    bool page_text_stale;

    if (!s_display_ready)
    {
        return;
    }
    trend_buffer_update(&s_trend, now);
    if (s_renderer.phase == RENDER_PHASE_IDLE && s_frame_rendering)
    {
        /* A status/reading-only update reaches IDLE without visiting the
         * trend-column completion path. Present it before accepting another
         * snapshot, otherwise the completed hidden page could be overwritten. */
        display_enable_after_initial_frame();
        return;
    }
    /* Publish immutable render snapshots only while idle. Queue a due trend
     * before text regions so a continuously dirty 10 Hz reading cannot starve
     * the 5 Hz graph; the region request is retained by the scheduler and runs
     * immediately after the two incremental trend phases. Active work is never
     * restarted, so every bitmap/graph slice progresses at 500 readings/s. */
    if (s_renderer.phase == RENDER_PHASE_IDLE)
    {
        /* Runtime status-bar glyph redraw is disabled while measuring the
         * reading path. The initial frame remains the authoritative status
         * raster; keeping STATUS out of due_regions prevents UPDATE_STATUS
         * from monopolizing the synchronous SPI/GE renderer. */
        due_regions = (uint8_t)(s_ui_dirty_regions & RENDER_DIRTY_READING);
        /* Keep collecting 500 Hz samples, but defer trend raster updates while
         * measuring the 30 Hz reading path. A 240-column trend pass contains
         * hundreds of synchronous GE/SPI transactions and can monopolize the
         * cooperative renderer for seconds. */
        trend_due = s_initial_page_pending &&
                    (now - s_trend_refresh_tick) >= 200u;
        display_due = (uint32_t)(now - s_display_due_tick) >=
                      DISPLAY_FRAME_PERIOD_MS;
        if (display_due && (due_regions != 0u || trend_due ||
                            s_perf_sample_count != 0u))
        {
            s_render_page = (uint8_t)(s_visible_page ^ 1u);
            s_render_full_page =
                (s_ready_page_mask & (uint8_t)(1u << s_render_page)) == 0u;
            main_display_format(&s_ui, &s_frame);
            perf_format_display(s_frame.gpib);
            (void)trend_buffer_project(&s_trend, now, s_trend_columns,
                                       TREND_MAX_COLUMNS);
            main_display_format_trend(&s_trend, now, s_frame.unit, &s_frame);
             s_trend_full_repaint = s_initial_page_pending ||
                                   /* The non-visible page receives the current visible trend
                                    * band before incremental columns are drawn, so compare this
                                    * snapshot with the visible-page scale. Comparing its stale
                                    * pre-copy cache would force a needless full trend rebuild. */
                                   s_page_trend_has_data[s_visible_page] !=
                                       s_frame.trend_has_data ||
                                   (s_frame.trend_has_data &&
                                    (s_page_trend_minimum[s_visible_page] !=
                                         s_frame.trend_minimum ||
                                     s_page_trend_maximum[s_visible_page] !=
                                         s_frame.trend_maximum));
            trend_needed = trend_due;
            if (begin_hidden_frame())
            {
                if ((due_regions & RENDER_DIRTY_READING) != 0u)
                    s_text_generation++;
                s_frame_text_generation = s_text_generation;
                page_text_stale = due_regions != 0u;
                if ((due_regions & RENDER_DIRTY_READING) != 0u)
                    s_text_refresh_tick = now;
                if (trend_due)
                    s_trend_refresh_tick = now;
                s_display_due_tick = now;
                /* Runtime status text is intentionally held at its initial
                 * raster. Its many synchronous glyph GE transactions can
                 * consume the entire display deadline; the reading and trend
                 * paths remain live for the performance measurement. */
                s_render_status_regions = false;
                if (page_text_stale)
                    render_scheduler_request_regions(&s_renderer, due_regions);
                if (trend_needed)
                {
                    render_scheduler_request_trend(&s_renderer);
                    s_frame_has_trend_update = true;
                }
                s_ui_dirty_regions &= (uint8_t)~due_regions;
            }
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
         * the unit at digit size (aligned with the value), a half-height
         * DC/AC suffix bottom-aligned with the reading, and the right info
         * panel as a 4-row rectangle of Excel-style cells (Zin / Range /
         * Rate / Status lamps). */
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
        /* The initial page prioritizes the reading and trend. The compact
         * metadata cells are painted by the first normal reading update, so
         * their many small glyph transactions cannot delay first reveal. */
        if (s_renderer.phase == RENDER_PHASE_INITIAL_READING)
        {
            render_scheduler_complete_phase(&s_renderer);
            s_render_item = 0u;
            return;
        }
        if (s_render_item >= first_info &&
            !s_render_status_regions)
        {
            render_scheduler_complete_phase(&s_renderer);
            s_render_item = 0u;
            return;
        }
        if (s_render_item >= first_info &&
            s_render_item < (uint8_t)(first_info + 16u))
        {
            if (!reading_draw_info((uint8_t)(s_render_item - first_info)))
                return;
            s_render_item++;
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
            /* Clear the trend region, then restore the info-style L-shaped
             * axis cells before drawing the grid and labels. */
            trend_draw_background();
            s_render_item++;
            return;
        }
        if (s_render_item >= 1u && s_render_item <= 4u)
        {
            uint8_t i = (uint8_t)(s_render_item - 1u);
            uint16_t y = (uint16_t)(MAIN_DISPLAY_PLOT_Y + i * MAIN_DISPLAY_PLOT_H / 3u);
            size_t label_width = strlen(s_frame.y_labels[i]) * FONT_TEXT_WIDTH;
            uint16_t label_x = label_width + 4u <= MAIN_DISPLAY_PLOT_X
                                   ? (uint16_t)(MAIN_DISPLAY_PLOT_X - 4u - label_width)
                                   : 0u;
            if (!ui_draw_text(label_x, trend_y_label_y(i), s_frame.y_labels[i], MAIN_DISPLAY_COLOR_CYAN))
                return;
            (void)ui_draw_line(MAIN_DISPLAY_PLOT_X, y, MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W, y, MAIN_DISPLAY_COLOR_GRID);
            s_render_item++;
            return;
        }
        if (s_render_item >= 5u && s_render_item <= 9u)
        {
            uint8_t i = (uint8_t)(s_render_item - 5u);
            uint16_t x = (uint16_t)(MAIN_DISPLAY_PLOT_X + i * MAIN_DISPLAY_PLOT_W / 4u);
            if (!ui_draw_text(trend_x_label_x(x, s_frame.x_labels[i]),
                              MAIN_DISPLAY_X_LABEL_Y,
                              s_frame.x_labels[i], MAIN_DISPLAY_COLOR_CYAN))
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
        if (!s_trend_full_repaint)
        {
            /* The existing labels/grid remain valid. The columns phase below
             * compares against this page's cache and touches changed pixels
             * only. */
            render_scheduler_complete_phase(&s_renderer);
            return;
        }
        if (s_render_item == 0u)
        {
            /* Pages are rendered independently after their one-time base
             * build. Clear the canvas and restore the info-style L-shaped
             * axis cells before drawing the new projection. */
            trend_draw_background();
            s_render_item = 1u;
            return;
        }
        /* Axis text owns x=0..95 only; the plot and resident grid start at 96.
         * Pair each local clear with its replacement before moving to the next
         * label. Thus a shorter label cannot leave stale pixels, while the
         * complete axis never disappears for several cooperative slices. */
        if (s_render_item < MAIN_DISPLAY_Y_LABEL_COUNT * 2u + 1u &&
            (s_render_item & 1u) != 0u)
        {
            uint8_t i = (uint8_t)((s_render_item - 1u) / 2u);
            uint16_t y = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                    i * MAIN_DISPLAY_PLOT_H / 3u);
            (void)ui_draw_line(MAIN_DISPLAY_PLOT_X, y,
                               MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W, y,
                               MAIN_DISPLAY_COLOR_GRID);
            s_render_item++;
            return;
        }
        if (s_render_item < MAIN_DISPLAY_Y_LABEL_COUNT * 2u + 1u)
        {
            uint8_t i = (uint8_t)((s_render_item - 1u) / 2u);
            size_t label_width = strlen(s_frame.y_labels[i]) * FONT_TEXT_WIDTH;
            uint16_t label_x = label_width + 4u <= MAIN_DISPLAY_PLOT_X
                                   ? (uint16_t)(MAIN_DISPLAY_PLOT_X - 4u - label_width)
                                   : 0u;
            if (!ui_draw_text(label_x, trend_y_label_y(i), s_frame.y_labels[i],
                              MAIN_DISPLAY_COLOR_CYAN))
                return;
            s_render_item++;
            return;
        }
        if (s_render_item < MAIN_DISPLAY_Y_LABEL_COUNT * 2u +
                                MAIN_DISPLAY_X_LABEL_COUNT * 2u + 1u)
        {
            uint8_t n = (uint8_t)(s_render_item -
                                  (MAIN_DISPLAY_Y_LABEL_COUNT * 2u + 1u));
            uint8_t i = (uint8_t)(n / 2u);
            uint16_t x = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                                    i * MAIN_DISPLAY_PLOT_W / 4u);
            if ((n & 1u) == 0u)
            {
                (void)ui_draw_line(x, MAIN_DISPLAY_PLOT_Y, x,
                                   MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H,
                                   MAIN_DISPLAY_COLOR_GRID);
                s_render_item++;
                return;
            }
            if (!ui_draw_text(trend_x_label_x(x, s_frame.x_labels[i]),
                              MAIN_DISPLAY_X_LABEL_Y,
                              s_frame.x_labels[i], MAIN_DISPLAY_COLOR_CYAN))
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
        /* Each column can issue up to three line commands, plus an erase and
         * grid restoration on updates.  Rendering all 240 columns in one
         * pass defeats the cooperative scheduler and can starve input. */
        uint16_t budget = 8u;
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
            trend_draw_column(s_render_column,
                              !initial_phase && !s_trend_full_repaint);
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
        const lt7680_panel_t panel = {320u, 960u, 16u};
        static const k2000_proto_cb_t proto_cb = {proto_on_event, proto_on_unknown};
        lt7680_status_t st;
        uint8_t status = 0u;

        hal_board_init();
        k2000_proto_init(&proto_cb);
        panel_transform_init(MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_UI_HEIGHT,
                             panel.width, panel.height);
        hal_uart_send_text("\r\nK2000 TFT build13 trend-layout v16");
#if K2000_DEMO_FEED
        hal_uart_send_text(" DEMO-FEED v16\r\n");
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
                        s_visible_page = 0u;
                        s_render_page = 1u;
                        st = lt7680_gfx_select_canvas_page(s_render_page);
                        if (st == LT7680_OK)
                            st = lt7680_gfx_clear(0x0000u);
                        if (st == LT7680_OK)
                        {
                            /* Clear the page that is selected by MISA too. Show
                             * this known-black page before the cooperative first
                             * frame starts drawing on the hidden page. */
                            st = lt7680_gfx_select_canvas_page(s_visible_page);
                            if (st == LT7680_OK)
                            {
                                /* The first cooperative frame starts on this
                                 * cleared canvas, so keep its software page
                                 * identity aligned with CVSSA. */
                                s_render_page = s_visible_page;
                                st = lt7680_gfx_clear(0x0000u);
                            }
                            if (st == LT7680_OK)
                                st = lt7680_gfx_present_page(s_visible_page);
                        }
                            if (st != LT7680_OK)
                            {
                                hal_uart_send_text("FAIL clear=");
                            hal_uart_send_hex8((uint8_t)st);
                            hal_uart_send_text("\r\n");
                            }
                            else
                            {
                                /* Validate the external RIF while the panel is still
                                 * blank. A failed probe leaves the internal font path
                                 * active; a valid header enables external glyphs. */
                                rif_init();
                                s_ready_page_mask = (uint8_t)(1u << s_visible_page);
                            s_display_enabled = false;
                            hal_uart_send_text("PASS black page visible, building hidden frame\r\n");
                            /* Build the complete first frame on page 1 while page 0
                             * remains visible. The completed hidden page is presented
                             * by reading_scene_render(). */
                            main_display_format(&s_ui, &s_frame);
                            main_display_format_trend(&s_trend, HAL_GetTick(), s_frame.unit,
                                                      &s_frame);
                            (void)trend_buffer_project(&s_trend, HAL_GetTick(),
                                                       s_trend_columns, TREND_MAX_COLUMNS);
                            s_frame_rendering = true;
                            s_frame_has_trend_update = true;
                            s_ui_dirty_regions = 0u;
                            s_initial_page_pending = true;
                            hal_uart_send_text("PASS framebuffer ready, building hidden frame\r\n");
                            s_display_ready = true;
                            hal_uart_send_text("\r\nINIT-OK\r\n");
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
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
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
