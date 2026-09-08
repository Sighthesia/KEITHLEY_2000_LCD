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
#include <limits.h>
#include <stdio.h>
#include "hal_board.h"
#include "font_digits.h"
#include "font_half.h"
#include "font_text.h"
#include "keypad.h"
#include "ui_layout.h"
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
#include "trend_axis.h"
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

/* Pause between glyph blits (ms): back-to-back BTE bursts contend with the
 * display scan for SDRAM bandwidth and leave sparse single-pixel sparkles.
 * A 1 ms gap per glyph keeps ~10 glyphs/frame inside the 33 ms budget. */
#ifndef RIF_BLIT_GAP_MS
#define RIF_BLIT_GAP_MS 0U
#endif

/* Change-diff bookkeeping for the reading band. Each cached tile carries an
 * opaque black background covering its full 64x128 UI cell, so re-blitting a
 * cell erases it; unchanged cells can skip the blit entirely and the whole
 * band clear becomes unnecessary. Item 0 of the reading phase plans this
 * frame's cells, keeps the ones identical to last frame (state KEPT), erases
 * vanished ones, and flags new/moved ones (state FRESH) for the drawer. */
#if RIF_BTE_RENDERER
typedef struct
{
    uint16_t x;
    uint16_t y;
    uint32_t kind;
    uint16_t code;
    uint8_t fresh; /* 1 = must blit this frame */
} rif_cell_t;

#define RIF_CELL_MAX 20u
static rif_cell_t s_cells[RIF_CELL_MAX];
static uint8_t s_cell_count;
static bool s_reading_diff;
static uint16_t s_prev_reading_color = 0xFFFFu;
static uint8_t s_prev_reading_nodata = 0xFFu;
static char s_prev_suffix[4];
static uint16_t s_prev_suffix_x;
static uint16_t s_prev_suffix_color;

static rif_cell_t *rif_cell_find(uint16_t x, uint16_t y, uint32_t kind,
                                 uint16_t code)
{
    uint8_t i;

    for (i = 0u; i < s_cell_count; i++)
    {
        if (s_cells[i].x == x && s_cells[i].y == y &&
            s_cells[i].kind == kind && s_cells[i].code == code)
            return &s_cells[i];
    }
    return NULL;
}
#endif

/* Demo feed: synthesize K2000 host frames on a timer so the full
 * UART->proto->reading_split->ui_model->trend_buffer->render pipeline can be
 * verified on the bench without an instrument. Values ramp up/down while the
 * unit/range table rotates (VDC/VAC/ADC/AAC/mVDC/mVAC/mADC/mAAC/OHM/kOHM/MOHM/
 * Hz/kHz/MHz/°C), exercising the split DC/AC half-height suffix, the
 * digit-size unit letters and the info panel lamps (REL/FILT/AUTO/MATH,
 * HOLD/TRIG, FAST/MED/SLOW rate). Units are limited to the 64x128 digit
 * charset (no U/Z/S glyphs; Flash too tight to add them). The DC/AC suffix is
 * the only half-height text; the mV/mA base units stay at digit size.
 * Set to 1 to enable; excluded from the normal build so the Flash budget is
 * unaffected. Keep the unit table in sync with sim/index.html. */
#define K2000_DEMO_FEED 1U

/* The sample clock and the display clock are deliberately independent.
 * K2000_DEMO_INPUT_HZ is the generated-field rate of the bench demo; the
 * default 10 Hz reproduces a normal K2000 sample flow. Raising it (only
 * for input-path testing, e.g. K2000_DEMO_INPUT_HZ=500) must NOT raise the
 * display rate: reading updates stay coalesced to the newest frame and
 * display commits remain bounded by DISPLAY_FRAME_PERIOD_MS.
 * NOTE (2026-08-30): a leftover 500 here feeds a full reading frame every
 * 2 ms — the documented regression from the 2026-08-22 fix: the trend
 * cursor storms at 50 buckets/s, single buckets get min/max-stretched ten
 * times over, and slots re-render several times per frame (visible
 * flicker). 10 Hz is the validated value (AGENTS 2026-08-22). */
#define K2000_DEMO_INPUT_HZ 500u
#if K2000_DEMO_FEED && (K2000_DEMO_INPUT_HZ == 0u || K2000_DEMO_INPUT_HZ > 1000u)
#error "K2000_DEMO_INPUT_HZ must be 1..1000"
#endif
#define DEMO_SAMPLE_PERIOD_MS ((uint32_t)(1000u / K2000_DEMO_INPUT_HZ))
#define DISPLAY_FRAME_PERIOD_MS 33u

/* Diagnostic performance baseline: keep only the authoritative reading path.
 * This deliberately bypasses the former trend/status/page-sync composition so
 * 500 Hz input can be evaluated against one bounded 30 Hz presentation path. */
#ifndef K2000_READING_ONLY_BASELINE
#define K2000_READING_ONLY_BASELINE 1u
#endif
#if K2000_READING_ONLY_BASELINE
#define READING_ONLY_LEGACY __attribute__((unused))
/* Production glyph path: BTE blits from the boot-staged SDRAM cache
 * (~1 ms/glyph). DIRECT_DMA=1 forces per-glyph serial-Flash DMA (~7 ms
 * each) — a leftover A/B diagnostic that stalls every range-change
 * rebuild under 500 Hz input. Pixels are identical either way. */
#define READING_ONLY_DIRECT_DMA 0u
#define READING_ONLY_PAGE_FLIP 1u
#define READING_ONLY_CLEAR_BAND 0u
/* MRWDP per-slot landing probe, diagnostic only (default OFF). */
#ifndef TREND_SWEEP_PROBE
#define TREND_SWEEP_PROBE 0
#endif
#else
#define READING_ONLY_LEGACY
#define READING_ONLY_DIRECT_DMA 0u
#define READING_ONLY_PAGE_FLIP 1u
#define READING_ONLY_CLEAR_BAND 1u
#endif
/* Readings repaint at most once per display period (30 Hz): the value
 * stream may be 500 Hz, but intermediate digits can never be shown and
 * each repaint costs ~20 glyph transfers. */

/* Input-path test build switch: set to 1 together with K2000_DEMO_INPUT_HZ=500
 * to measure the input->reading pipeline without trend work competing for
 * scheduler turns. Must stay 0 in default/acceptance builds. */
#ifndef K2000_TREND_TEST_DISABLE
#define K2000_TREND_TEST_DISABLE 0
#endif

/* Runtime composition writes ONLY the hidden render page (validated on
 * hardware, Task 3); initialization and the blanked first frame keep the
 * verified dual-page writes so both canvases start identical. */

/* Dirty bands of one composition, in UI space. They drive the frame-begin
 * page synchronization: only bands a previous frame touched can differ
 * between the two SDRAM pages, so only those are BTE-copied -- never a
 * whole-canvas clone. */
#define FRAME_REGION_STATUS  0x01u
#define FRAME_REGION_READING 0x02u
#define FRAME_REGION_TREND   0x04u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static int16_t internal_temperature_read(void);
static void internal_temperature_init(void);
static void refresh_runtime_snapshot(void);
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
/* Bands touched since the render page last caught up with the visible one.
 * Consumed by the frame-begin BTE band copies (hidden-page rendering). */
static uint8_t s_frame_regions;
static uint8_t s_text_generation;
static uint8_t s_page_text_generation[2];
static uint8_t s_frame_text_generation;
static bool s_frame_has_trend_update;
static bool s_render_status_regions;
static uint8_t s_status_info_dirty_rows;
static int16_t s_internal_temperature_tenths = INT16_MIN;
/* Header keep-alive tick: stamped on EVERY composition (reading or
 * header-only) that serves the header band from the latest snapshot.
 * It must NOT double as the ADC cadence tick (see s_internal_temp_tick):
 * while the SHT3x is online the fallback branch below never runs, so a
 * shared tick froze here and left header_due permanently true -- every
 * idle gap then spawned a header-only filler frame that bypasses the
 * 33 ms throttle (it never updates s_display_due_tick), doubling the
 * commit rate with a 2-3-quick + 1-long slow-motion rhythm. */
static uint32_t s_temperature_tick;
/* Last internal-ADC sample, fallback path only (no SHT3x on board). */
static uint32_t s_internal_temp_tick;
/* Last good SHT3x sample for the header runtime field. INT16_MIN = none
 * yet (header shows the internal temperature with "--%" RH instead). */
static int16_t s_sht_temp_tenths = INT16_MIN;
static int16_t s_sht_rh_pct = INT16_MIN;
static uint32_t s_sht_tick;
#define SHT3X_SENSOR_PERIOD_MS 30000u
static bool s_is_header_only;

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
/* Set when an erase punched through a gridline; the owning page's grid
 * is redrawn ONCE at pass end instead of per-column (a sloped trace
 * changes nearly every column, and per-column restoration multiplied
 * the GE traffic into multi-hundred-ms passes). */
static bool s_trend_grid_dirty[2];
static bool s_page_trend_has_data[2];
static float s_page_trend_minimum[2];
static float s_page_trend_maximum[2];
/* Explicit trend-axis state. The RESIDENT axis is what is currently
 * painted on both SDRAM pages (runtime frames level the sibling with band
 * copies at frame start, so one identity serves both). The acceptance
 * rules (new unit -> immediate rebuild, uninitialized resident -> rebuild
 * once, same-unit drift -> keep resident, out-of-range data -> candidate,
 * candidate persisted TREND_AXIS_CANDIDATE_TIMEOUT_MS -> rebuild) are
 * implemented by the
 * shared pure module trend_axis.c and covered by its host regression
 * tests; this site only owns page history, projection and publication. */
static main_display_trend_axis_t s_trend_axis_resident;
static trend_axis_candidate_t READING_ONLY_LEGACY s_trend_axis_candidate;
static bool READING_ONLY_LEGACY s_trend_full_repaint;
/* Same-unit overflow rescale: only Y labels change meaning -- plot
 * surface, gridlines and X labels are pixel-identical. Skip the whole
 * chart-panel background fill (~300 ms of GE fills) and just relabel. */
static bool READING_ONLY_LEGACY s_trend_relabel_only;
static main_display_frame_t s_frame;

static void refresh_runtime_snapshot(void)
{
    uint32_t now = HAL_GetTick();
    int16_t temp_tenths;

    /* SHT3x ambient sensor (PB15=SCL/PB14=SDA) at a slow cadence: one
     * blocking single-shot read costs ~6 ms (low repeatability), so it
     * runs every 30 s -- far below the 33 ms display budget and the
     * 200 ms stall threshold, and off every frame budget. High
     * repeatability (≈17 ms every 5 s) stretched one reading frame per
     * period, visible in slow-motion as an occasional longer interval
     * (same hitch class as the PERF UART print, fixed by 5-div).
     * A failed read keeps the last good sample; boards without the
     * sensor fall back to the MCU internal temperature with "--%" RH. */
    if ((uint32_t)(now - s_sht_tick) >= SHT3X_SENSOR_PERIOD_MS ||
        s_sht_temp_tenths == INT16_MIN) {
        int32_t t_mc = 0;
        int32_t rh_milli = 0;

        if (hal_sht3x_read_milli(&t_mc, &rh_milli)) {
            s_sht_temp_tenths = (int16_t)(t_mc / 100);
            s_sht_rh_pct = (int16_t)(rh_milli / 1000);
        }
        s_sht_tick = now;
    }
    if (s_sht_temp_tenths == INT16_MIN) {
        if ((uint32_t)(now - s_internal_temp_tick) >= 1000u ||
            s_internal_temperature_tenths == INT16_MIN) {
            s_internal_temperature_tenths = internal_temperature_read();
            s_internal_temp_tick = now;
        }
        temp_tenths = s_internal_temperature_tenths;
    } else {
        temp_tenths = s_sht_temp_tenths;
    }
    /* Any composition serves the header: stamp the keep-alive tick so a
     * header-only filler frame only fires after a full second with no
     * composition at all (silent host), never between reading frames. */
    s_temperature_tick = now;
    main_display_format_runtime(&s_frame, temp_tenths, s_sht_rh_pct, now);
}
static uint32_t s_text_refresh_tick;
static uint32_t READING_ONLY_LEGACY s_trend_refresh_tick;
static uint32_t s_display_due_tick;
static uint32_t s_perf_frame_start_tick;
static uint32_t s_perf_last_frame_ms;
static uint32_t s_perf_max_frame_ms;
static uint32_t s_perf_window_tick;
static uint16_t s_perf_window_frames;
static uint16_t s_perf_fps;

static uint32_t s_perf_sample_count;
static uint32_t s_perf_sample_missed;
/* Main-loop watchdog: records the worst iteration gap plus the renderer
 * phase at that moment. A [STALL] line prints once per event so a hang
 * leaves its location in the serial log even if UART later dies. */
static uint32_t s_loop_last_tick;
/* DWT PC sampler state, written from SysTick at 1 kHz. Reading these via
 * SWD during a freeze shows exactly where execution is stuck, including
 * inside dead waits that no main-loop instrumentation can observe. */
volatile uint32_t s_irq_pc;
volatile uint32_t s_irq_pc_ring[16];
volatile uint8_t  s_irq_pc_idx;
/* Stack canary: paint the deepest 64 bytes of the 1 KB stack once at
 * boot; the heartbeat checks the pattern. Overflow eats it bottom-up. */
#define WDT_STACK_LOW   ((volatile uint32_t *)0x20004C00u)
#define WDT_STACK_CANARY 0xC1A5C1A5u
static uint32_t s_loop_max_gap_ms;
static uint8_t s_loop_stall_phase;
static uint8_t s_loop_stall_printed;
static uint32_t s_perf_fields_window;
static uint32_t s_perf_reading_frames_window;
static uint32_t s_perf_display_commits_window;
static uint32_t s_perf_axis_rebuilds_window;
static uint32_t s_perf_trend_columns_window;
/* Low-cost diagnosis counters. They are printed with the existing PERF
 * record so the probes do not add UART work to the render path. */
static uint32_t s_dbg_function_change_window;
static uint32_t s_dbg_info_repaint_window;
static uint32_t s_dbg_stale_kill_window;
static uint32_t s_dbg_present_hold_window;
/* Per-stage main-loop cost (ms) attributed to the stage active during each
 * interval between reading_only_render() entries. IDLE=0..PRESENT=8 in enum
 * order. Printed with PERF; pinpoints which stage owns a long frame. */
static uint32_t s_dbg_stage_ms_window[9];
static uint32_t s_dbg_stage_max_window[9];
static uint32_t s_dbg_last_render_tick;
static uint8_t s_dbg_last_stage;
static bool s_dbg_stage_armed;
/* Present-to-present jitter tap (slow-motion interval diagnosis, 2026-09-08):
 * counts display-commit intervals above the normal 33 ms cadence. mid =
 * (50,100] ms (one stretched frame, slow-mo only), big = >100 ms (a real
 * stall). No UART on the hot path; printed with PERF. */
static uint32_t s_perf_last_present_tick;
static uint32_t s_perf_jit_mid_window;
static uint32_t s_perf_jit_big_window;

#if K2000_READING_ONLY_BASELINE
typedef enum {
    READING_ONLY_IDLE = 0,
    READING_ONLY_STATUS,
    READING_ONLY_INFO,
    READING_ONLY_CLEAR,
    READING_ONLY_VALUE,
    READING_ONLY_UNIT,
    READING_ONLY_SUFFIX,
    READING_ONLY_TREND,
    READING_ONLY_PRESENT,
} reading_only_stage_t;

static bool s_reading_only_dirty;
static reading_only_stage_t s_reading_only_stage;
static uint32_t s_reading_only_generation;
static uint32_t s_reading_only_frame_generation;
static uint32_t s_reading_only_render_errors;
static lt7680_status_t s_reading_only_last_error;
static bool s_reading_only_io_error;
static char s_reading_only_page_unit[2][UI_MODEL_MAX_UNIT];
static char s_reading_only_page_suffix[2][4];
static uint16_t s_reading_only_page_unit_x[2];
static uint16_t s_reading_only_page_unit_w[2];
static uint16_t s_reading_only_page_suffix_x[2];
static uint16_t s_reading_only_page_suffix_color[2];
static char s_reading_only_page_value[2][UI_MODEL_MAX_FIELD];
static uint16_t s_reading_only_page_value_x[2];
static uint16_t s_reading_only_page_value_color[2];
static uint8_t s_reading_only_value_index;
static bool s_reading_only_page_status_valid[2];
static uint8_t s_reading_only_page_status_lamps[2];
static char s_reading_only_page_row1[2][MAIN_DISPLAY_META_MAX];
/* Brand-row extras snapshot: white SHIFT/REAR text right after the logo
 * (an active SHIFT also turns the whole row-1 background blue). */
static char s_reading_only_page_extra[2][16];
static bool s_reading_only_page_shift[2];
static bool s_reading_only_page_rear[2];
static bool s_reading_only_page_info_valid[2];
/* TRIGGER dot blink (ADR-0004): per-page last painted dot (-1 = none/erased,
 * 0 = off, 1 = on), shared 250 ms phase, pending flag forcing INFO stage. */
static int8_t s_reading_only_page_trig_dot[2];
static bool s_trig_dot_phase;
static uint32_t s_trig_dot_tick;
static bool s_trig_dot_pending;
static char s_reading_only_page_active_status[2][MAIN_DISPLAY_META_MAX];
static char s_reading_only_page_temperature[2][20];
static char s_reading_only_page_uptime[2][12];
static char s_reading_only_page_brand[2][20];
static char s_reading_only_page_function[2][MAIN_DISPLAY_FUNCTION_MAX];
static char s_reading_only_page_impedance[2][32];
static char s_reading_only_page_range[2][32];
static char s_reading_only_page_rate[2][32];
static uint8_t s_reading_only_page_info_lamps[2];
static bool s_reading_only_page_trend_bg_valid[2];
static char s_reading_only_page_trend_unit[2][TREND_UNIT_ID_MAX];
static bool s_trend_rebuild_transaction;
/* The new reading is painted before the new trend background. Once the
 * background is ready, only the deferred rows need painting; replaying
 * CLEAR/VALUE/UNIT/SUFFIX doubled the gear-change work. */
static bool s_deferred_row_pending;
/* NOTE (tautology audit): an earlier done/clone_ok mask ("skip when the
 * sibling was painted") was removed — any SOUND skip (pixels already
 * correct) is exactly the cache-match below, so the mask was either a
 * no-op or poison (post-defer stale caches protected from repainting:
 * old-badge/new-badge alternation). Cache-match alone is complete. */
/* Row snapshot: second-scale churn (uptime/temperature/lamp bits/measured
 * rate) straddles the two pages' row paints during slow storm compositions,
 * so every rotation visibly updates the rows twice. Freeze just the churn
 * fields at the first post-invalidate frame; rotation content (function /
 * impedance / brand) stays live — it changes once and both pages converge
 * on it via cache-match. Reading + trend stay live. No axis requirement
 * for the core (axis-independence is the point: temp/uptime/bits need no
 * axis, and waiting for it deadlocks past the transaction window). Cleared
 * when quiet; pre-data frames never snapshot (temp/uptime empty). */
static bool s_row_snap_taken;
static bool s_row_snap_active[STATUS_BAR_CORE_COUNT];
static char s_row_snap_rate[MAIN_DISPLAY_META_MAX];
static char s_row_snap_temperature[20];
static char s_row_snap_uptime[12];
static bool s_row_snap_shift;
static bool s_row_snap_rear;
/* Range joins the snapshot at the first axis-valid frame of the episode.
 * The axis transition mints a different range string per composition
 * ("--", AUTO-bit flips, unit change), and without this the two pages
 * paint different ranges and alternate visibly on slow storm flips.
 * INFO-defer covers the axis-less first pass, so "--" is never baked. */
static bool s_row_snap_range_taken;
static char s_row_snap_range[MAIN_DISPLAY_META_MAX];
static uint32_t s_row_snap_tick;
/* Shared stat-slot snapshot: the two pages build one frame apart and the
 * sliding window would otherwise mint different last digits per page —
 * alternating flips then flicker between two near-identical values. First
 * page to build captures; the sibling repaints from the snapshot. */
static char s_trend_stat_snapshot[3][24];
static bool s_trend_stat_snap_valid;

static uint16_t s_reading_only_trend_column;
static bool s_reading_only_trend_bg_failed;
static uint32_t s_trend_scroll_ms;
static bool s_trend_axis_valid;
static float s_trend_axis_min;
static float s_trend_axis_max;
static char s_trend_axis_unit[TREND_UNIT_ID_MAX];
static bool s_reading_only_page_trend_curve_valid[2];
#define TREND_PIP_SOURCE_ADDRESS 0x00400000u
#define TREND_PIP_SOURCE_WIDTH 96u
#define TREND_PIP_SOURCE_HEIGHT 4096u
static bool s_trend_pip_ready;
static uint32_t s_trend_pip_head;
static uint32_t s_trend_pip_last_tick;
static uint16_t s_trend_pip_prev_x;
static uint32_t s_trend_pip_prev_y;
static bool s_trend_pip_prev_valid;

static void reading_only_invalidate_trend_pages(void)
{
    uint8_t page;

    s_trend_scroll_ms = 0u;
    s_reading_only_trend_column = 0u;
    s_trend_stat_snap_valid = false;
    /* No transaction before the first present: the visible page is still
     * boot-black, so there is nothing to preserve — and starting one here
     * freezes STATUS/INFO out of the virgin pipeline, leaving the first
     * committed page without its top two rows. */
    if (s_display_enabled)
        s_trend_rebuild_transaction = true;
    s_row_snap_taken = false;
    s_row_snap_range_taken = false;
    /* NOTE: the cached unit is deliberately KEPT (rotation path): a stale
     * unit tells the background pass this is a rotation (targeted repaint
     * of dynamic strips) rather than a virgin page (full build). Boot pages
     * start with empty units, which forces the full build. */
    for (page = 0u; page < 2u; page++)
    {
        s_reading_only_page_trend_bg_valid[page] = false;
        s_reading_only_page_trend_curve_valid[page] = false;
        memset(s_drawn_trend_y0[page], 0, sizeof(s_drawn_trend_y0[page]));
        memset(s_drawn_trend_y1[page], 0, sizeof(s_drawn_trend_y1[page]));
        memset(s_drawn_trend_occupied[page], 0,
               sizeof(s_drawn_trend_occupied[page]));
        s_trend_grid_dirty[page] = false;
    }
}

/* Probed on-target 2026-08-30: both PIP1 and PIP2 accept the full datasheet
 * V4.2 10.3 sequence (registers verified by readback, VDIR conflict removed
 * via panel MADCTL) but never composite a saturated test pattern on this
 * die.  The trend path therefore stays on the main-window renderer; these
 * helpers are kept dormant for future silicon revisions. */
static bool READING_ONLY_LEGACY trend_pip_init(void)
{
    lt7680_rect_t clear = {0u, 0u, TREND_PIP_SOURCE_WIDTH,
                           TREND_PIP_SOURCE_HEIGHT};
    lt7680_status_t st;
    uint8_t step = 0u;

    st = lt7680_gfx_pip1_enable(false);
    if (st == LT7680_OK)
    {
        step = 1u;
        st = lt7680_gfx_set_surface(TREND_PIP_SOURCE_ADDRESS,
                                    TREND_PIP_SOURCE_WIDTH,
                                    TREND_PIP_SOURCE_HEIGHT);
    }
    if (st == LT7680_OK)
    {
        step = 2u;
        st = lt7680_gfx_fill_rect(&clear, MAIN_DISPLAY_COLOR_BG);
    }
    if (st == LT7680_OK)
    {
        static const uint16_t value_grid_x[4] = {0u, 28u, 56u, 91u};
        static const uint16_t time_grid_y[5] = {0u, 210u, 420u, 630u, 840u};
        uint8_t i;

        step = 3u;
        for (i = 0u; i < 4u && st == LT7680_OK; i++)
            st = lt7680_gfx_surface_draw_line(
                value_grid_x[i], 0u, value_grid_x[i],
                TREND_PIP_SOURCE_HEIGHT - 1u, MAIN_DISPLAY_COLOR_GRID);
        for (i = 0u; i < 5u && st == LT7680_OK; i++)
            st = lt7680_gfx_surface_draw_line(
                0u, time_grid_y[i], TREND_PIP_SOURCE_WIDTH - 5u,
                time_grid_y[i], MAIN_DISPLAY_COLOR_GRID);
    }
    if (st == LT7680_OK)
    {
        step = 4u;
        st = lt7680_gfx_pip1_configure(TREND_PIP_SOURCE_ADDRESS,
                                       TREND_PIP_SOURCE_WIDTH, 196u, 96u,
                                       92u, 840u, 0u, 0u);
    }
    if (st == LT7680_OK)
    {
        /* Restore BOTH the canvas and the active window: set_surface()
         * repointed AW to the PIP surface, and a stale 96x4096 active window
         * would clip every later main-page GE operation. */
        step = 5u;
        st = lt7680_gfx_set_surface(0u, 320u, 960u);
        if (st == LT7680_OK)
            st = lt7680_gfx_select_canvas_page(0u);
    }
    if (st == LT7680_OK)
    {
        step = 6u;
        st = lt7680_gfx_pip1_enable(true);
    }
    if (st != LT7680_OK)
    {
        hal_uart_send_text("[PIP] fail step=");
        hal_uart_send_hex8(step);
        hal_uart_send_text(" st=");
        hal_uart_send_hex8((uint8_t)st);
        hal_uart_send_text("\r\n");
    }
    s_trend_pip_ready = st == LT7680_OK;
    s_trend_pip_head = 0u;
    s_trend_pip_last_tick = HAL_GetTick();
    s_trend_pip_prev_valid = false;
    return s_trend_pip_ready;
}

static void trend_pip_reset(void);

static bool READING_ONLY_LEGACY trend_pip_update(uint32_t now)
{
    uint32_t head;
    uint16_t y;
    uint16_t plot_x;
    lt7680_status_t st;

    if (!s_trend_pip_ready || !s_frame.trend_has_data)
        return true;
    if (s_trend_pip_head >= TREND_PIP_SOURCE_HEIGHT - 2u)
    {
        trend_pip_reset();
        if (!s_trend_pip_ready)
            return false;
    }
    head = ((uint32_t)(now - s_trend_pip_last_tick) * 840u) / 10000u;
    if (head == 0u)
        return true;
    s_trend_pip_last_tick = now;
    s_trend_pip_head += head;
    y = (uint16_t)s_trend_pip_head;
    plot_x = main_display_trend_plot_y(
        s_trend_columns[TREND_MAX_COLUMNS - 1u].maximum,
        s_frame.trend_minimum, s_frame.trend_maximum);
    st = lt7680_gfx_set_surface(TREND_PIP_SOURCE_ADDRESS,
                                TREND_PIP_SOURCE_WIDTH,
                                TREND_PIP_SOURCE_HEIGHT);
    if (st == LT7680_OK && s_trend_pip_prev_valid)
        st = lt7680_gfx_surface_draw_line(
            s_trend_pip_prev_x, (uint16_t)(s_trend_pip_prev_y),
            plot_x, y, MAIN_DISPLAY_COLOR_GREEN);
    if (st == LT7680_OK)
        st = lt7680_gfx_pip1_set_source_y(
            s_trend_pip_head > 840u ? (uint16_t)(s_trend_pip_head - 840u) : 0u);
    if (st == LT7680_OK)
    {
        st = lt7680_gfx_set_surface(0u, 320u, 960u);
        if (st == LT7680_OK)
            st = lt7680_gfx_select_canvas_page(s_render_page);
    }
    if (st != LT7680_OK)
    {
        (void)lt7680_gfx_pip1_enable(false);
        s_trend_pip_ready = false;
        s_reading_only_last_error = st;
        s_reading_only_io_error = true;
        return false;
    }
    s_trend_pip_prev_x = plot_x;
    s_trend_pip_prev_y = s_trend_pip_head;
    s_trend_pip_prev_valid = true;
    return true;
}

static void trend_pip_reset(void)
{
    lt7680_rect_t clear = {0u, 0u, TREND_PIP_SOURCE_WIDTH,
                           TREND_PIP_SOURCE_HEIGHT};
    lt7680_status_t st;

    (void)lt7680_gfx_pip1_enable(false);
    st = lt7680_gfx_set_surface(TREND_PIP_SOURCE_ADDRESS,
                                TREND_PIP_SOURCE_WIDTH,
                                TREND_PIP_SOURCE_HEIGHT);
    if (st == LT7680_OK)
        st = lt7680_gfx_fill_rect(&clear, MAIN_DISPLAY_COLOR_BG);
    if (st == LT7680_OK)
        st = lt7680_gfx_pip1_set_source_y(0u);
    if (st == LT7680_OK)
    {
        st = lt7680_gfx_set_surface(0u, 320u, 960u);
        if (st == LT7680_OK)
            st = lt7680_gfx_select_canvas_page(s_render_page);
    }
    if (st == LT7680_OK)
        st = lt7680_gfx_pip1_enable(true);
    s_trend_pip_head = 0u;
    s_trend_pip_last_tick = HAL_GetTick();
    s_trend_pip_prev_valid = false;
    if (st != LT7680_OK)
        s_trend_pip_ready = false;
}
#endif

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

static void READING_ONLY_LEGACY perf_format_display(char *out)
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

/* Diagnostic: read the visible canvas back through MRWDP and print the
 * trend plot as ASCII (trace=#, bg=., other=?). Two consecutive frames
 * make any "flicker" visible as literal pixel motion, independent of the
 * renderer's assumptions. */
static uint16_t trend_sweep_slot_of_bucket(uint32_t bucket);
static uint32_t s_sweep_cycle;
#define K2000_TREND_DUMP 0
/* One-shot row-band pixel dump (default OFF): reads the VISIBLE page's
 * status/info text rows via MRWDP and prints raw RGB565 per 4 UI px, so a
 * covered glyph ("Range" missing "Ran") can be located without a camera.
 * Enable temporarily, capture one dump, disable again — hundreds of slow
 * reads stall the pipeline while active. */
#define K2000_ROW_DUMP 0
#if K2000_ROW_DUMP
static void row_debug_dump(void);
#endif

#if K2000_TREND_DUMP
static void trend_debug_dump(void)
{
    uint16_t i;
    uint16_t fx, fy;

    (void)lt7680_gfx_select_canvas_page(s_visible_page);
    (void)lt7680_gfx_set_canvas_width(320u);
    hal_uart_send_text("\r\nDUMP vis=");
    hal_uart_send_hex8(s_visible_page);
    hal_uart_send_text(" slot=");
    perf_send_u32(s_trend.has_sample
                      ? trend_sweep_slot_of_bucket(s_trend.newest_bucket)
                      : 0u);
    hal_uart_send_text(" cyc=");
    perf_send_u32(s_sweep_cycle);
    for (i = 0u; i < 12u; i++)
    {
        uint16_t ui_x = (uint16_t)(MAIN_DISPLAY_PLOT_X + i * 70u);
        uint16_t y;
        char buf[2] = {'-', '-'};

        if (ui_x < MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W)
        {
            for (y = MAIN_DISPLAY_PLOT_Y; y < MAIN_DISPLAY_PLOT_Y +
                                              MAIN_DISPLAY_PLOT_H;
                 y += 2u)
            {
                uint16_t px = 0u;

                panel_transform_ui_to_fb(ui_x, y, &fx, &fy);
                if (lt7680_gfx_peek_pixel(fx, fy, &px) == LT7680_OK &&
                    (px == MAIN_DISPLAY_COLOR_GREEN ||
                     px == MAIN_DISPLAY_COLOR_GREEN_DIM))
                {
                    buf[0] = "0123456789ABCDEF"[(y - MAIN_DISPLAY_PLOT_Y) >> 4];
                    buf[1] = "0123456789ABCDEF"[(y - MAIN_DISPLAY_PLOT_Y) & 0xFu];
                    break;
                }
            }
        }
        hal_uart_send((const uint8_t *)buf, 2u);
        hal_uart_send((const uint8_t *)" ", 1u);
    }
    hal_uart_send_text("\r\n");
    (void)lt7680_gfx_select_canvas_page(s_render_page);
    (void)lt7680_gfx_set_canvas_width(320u);
}

#endif
static uint16_t s_rif_bte_hits;
static uint16_t s_rif_bte_misses;
static uint32_t s_prof_fill_ms;
static uint32_t s_prof_fill_n;
static uint32_t s_prof_dma_ms;
static uint32_t s_prof_dma_n;
static uint32_t s_sweep_cursor_bucket;
static uint32_t s_sweep_scale_changes;
static uint32_t s_sweep_epoch_resets;
static uint32_t s_sweep_px_ok;
static uint32_t s_sweep_px_missed;

/* Chunked PERF emitter. The full line is ~440 B ~= 38 ms of blocking
 * UART: emitting it at once stretched one present interval past 50 ms
 * every 5 s (slow-motion-visible mid-jitter). Window counters are
 * snapshotted once, then emitted ~110 B per frame over the next 4 frames
 * so no single present interval grows past ~30 ms. ~240 B static. */
typedef struct {
    uint32_t fps, frame_ms, max_ms, samples, missed;
    uint32_t bte_hit, bte_miss, fl_ms, fl_n, dm_ms, dm_n;
    uint32_t gap, jit_mid, jit_big;
    uint32_t fields, reading_frames, commits, rebuilds, columns;
    uint32_t func, info, stale, hold;
    uint32_t stg[9];
    uint32_t stgm[9];
    uint32_t sw_behind, sw_scale, sw_reset, pxok, pxmiss;
    uint32_t reading_errors, last_error;
    char axu[TREND_UNIT_ID_MAX];
    char rng[MAIN_DISPLAY_META_MAX];
} perf_snapshot_t;
static perf_snapshot_t s_perf_tx_snap;
static uint8_t s_perf_tx_step;  /* 0 = idle, 1..4 = emitting */

static void perf_note_present(void)
{
    uint32_t now = HAL_GetTick();
    if (s_perf_last_present_tick != 0u)
    {
        uint32_t d = now - s_perf_last_present_tick;
        if (d > 100u) s_perf_jit_big_window++;
        else if (d > 50u) s_perf_jit_mid_window++;
    }
    s_perf_last_present_tick = now;
}

static void perf_record_frame(void)
{
    uint32_t now = HAL_GetTick();

    s_perf_last_frame_ms = now - s_perf_frame_start_tick;
    if (s_perf_last_frame_ms > s_perf_max_frame_ms)
        s_perf_max_frame_ms = s_perf_last_frame_ms;
    s_perf_window_frames++;
    if ((uint32_t)(now - s_perf_window_tick) >= 1000u)
    {
        /* Counters stay 1 s windows; the ~450 B print costs ~40 ms of
         * blocking UART, so it goes out every 5th window (1 Hz hitch). */
        static uint8_t print_div = 0u;
        bool print_this;
        s_perf_fps = s_perf_window_frames;
        s_perf_window_frames = 0u;
        s_perf_window_tick = now;
        print_div = (uint8_t)(print_div + 1u);
        print_this = (print_div >= 5u);
        if (print_this) print_div = 0u;
        if (!print_this)
        {
            s_loop_max_gap_ms = 0u;
            s_sweep_px_ok = 0u;
            s_sweep_px_missed = 0u;
            s_prof_fill_ms = 0u; s_prof_fill_n = 0u;
            s_prof_dma_ms = 0u; s_prof_dma_n = 0u;
            s_perf_fields_window = 0u;
            s_perf_reading_frames_window = 0u;
            s_perf_display_commits_window = 0u;
            s_perf_axis_rebuilds_window = 0u;
            s_perf_trend_columns_window = 0u;
            s_dbg_function_change_window = 0u;
            s_dbg_info_repaint_window = 0u;
            s_dbg_stale_kill_window = 0u;
            s_dbg_present_hold_window = 0u;
            memset(s_dbg_stage_ms_window, 0, sizeof(s_dbg_stage_ms_window));
            memset(s_dbg_stage_max_window, 0, sizeof(s_dbg_stage_max_window));
            s_perf_jit_mid_window = 0u;
            s_perf_jit_big_window = 0u;
            /* A 4-frame drain always finishes inside its window (4 frames
             * << 1 s); if it ever crosses a boundary, keep draining below
             * instead of stalling the line until the next print window. */
            if (s_perf_tx_step == 0u)
                return;
        }
        if (print_this)
        {
            /* Snapshot once, emit over the next 4 frames (see below).
             * Overlap is impossible by construction (4 frames << 5 s); if
             * it ever happened, keep window cadence and drop the print. */
            if (s_perf_tx_step == 0u)
            {
                uint8_t i;
                const char *p;
                s_perf_tx_snap.fps = s_perf_fps;
                s_perf_tx_snap.frame_ms = s_perf_last_frame_ms;
                s_perf_tx_snap.max_ms = s_perf_max_frame_ms;
                s_perf_tx_snap.samples = s_perf_sample_count;
                s_perf_tx_snap.missed = s_perf_sample_missed;
                s_perf_tx_snap.bte_hit = s_rif_bte_hits;
                s_perf_tx_snap.bte_miss = s_rif_bte_misses;
                s_perf_tx_snap.fl_ms = s_prof_fill_ms;
                s_perf_tx_snap.fl_n = s_prof_fill_n;
                s_perf_tx_snap.dm_ms = s_prof_dma_ms;
                s_perf_tx_snap.dm_n = s_prof_dma_n;
                s_perf_tx_snap.gap = s_loop_max_gap_ms;
                s_perf_tx_snap.jit_mid = s_perf_jit_mid_window;
                s_perf_tx_snap.jit_big = s_perf_jit_big_window;
                s_perf_tx_snap.fields = s_perf_fields_window;
                s_perf_tx_snap.reading_frames = s_perf_reading_frames_window;
                s_perf_tx_snap.commits = s_perf_display_commits_window;
                s_perf_tx_snap.rebuilds = s_perf_axis_rebuilds_window;
                s_perf_tx_snap.columns = s_perf_trend_columns_window;
                s_perf_tx_snap.func = s_dbg_function_change_window;
                s_perf_tx_snap.info = s_dbg_info_repaint_window;
                s_perf_tx_snap.stale = s_dbg_stale_kill_window;
                s_perf_tx_snap.hold = s_dbg_present_hold_window;
                for (i = 0u; i < 9u; i++)
                {
                    s_perf_tx_snap.stg[i] = s_dbg_stage_ms_window[i];
                    s_perf_tx_snap.stgm[i] = s_dbg_stage_max_window[i];
                }
                s_perf_tx_snap.sw_behind = s_trend.has_sample
                    ? (s_trend.newest_bucket > s_sweep_cursor_bucket
                        ? s_trend.newest_bucket - s_sweep_cursor_bucket : 0u)
                    : 0u;
                s_perf_tx_snap.sw_scale = s_sweep_scale_changes;
                s_perf_tx_snap.sw_reset = s_sweep_epoch_resets;
                s_perf_tx_snap.pxok = s_sweep_px_ok;
                s_perf_tx_snap.pxmiss = s_sweep_px_missed;
                s_perf_tx_snap.reading_errors = s_reading_only_render_errors;
                s_perf_tx_snap.last_error = (uint32_t)s_reading_only_last_error;
                p = s_trend_axis_unit;
                for (i = 0u; i < TREND_UNIT_ID_MAX - 1u && p[i] != '\0'; i++)
                    s_perf_tx_snap.axu[i] = p[i];
                s_perf_tx_snap.axu[i] = '\0';
                p = s_frame.range;
                for (i = 0u; i < MAIN_DISPLAY_META_MAX - 1u && p[i] != '\0'; i++)
                    s_perf_tx_snap.rng[i] = p[i];
                s_perf_tx_snap.rng[i] = '\0';
                s_perf_tx_step = 1u;
            }
            s_loop_max_gap_ms = 0u;
            s_prof_fill_ms = 0u; s_prof_fill_n = 0u;
            s_prof_dma_ms = 0u; s_prof_dma_n = 0u;
            s_perf_fields_window = 0u;
            s_perf_reading_frames_window = 0u;
            s_perf_display_commits_window = 0u;
            s_perf_axis_rebuilds_window = 0u;
            s_perf_trend_columns_window = 0u;
            s_dbg_function_change_window = 0u;
            s_dbg_info_repaint_window = 0u;
            s_dbg_stale_kill_window = 0u;
            s_dbg_present_hold_window = 0u;
            memset(s_dbg_stage_ms_window, 0, sizeof(s_dbg_stage_ms_window));
            memset(s_dbg_stage_max_window, 0, sizeof(s_dbg_stage_max_window));
            s_perf_jit_mid_window = 0u;
            s_perf_jit_big_window = 0u;
            s_sweep_px_ok = 0u;
            s_sweep_px_missed = 0u;
        }
    }
    /* Emit at most one part per frame (runs on EVERY present, not just on
     * window boundaries): each part is ~110 B ~= 10 ms, so the present
     * interval never notices. Parts must stay in order; "\r\n"
     * terminates the line in the last part. */
    switch (s_perf_tx_step)
        {
        case 1u:
            hal_uart_send_text("PERF fps=");
            perf_send_u32(s_perf_tx_snap.fps);
            hal_uart_send_text(" frame-ms=");
            perf_send_u32(s_perf_tx_snap.frame_ms);
            hal_uart_send_text(" max-ms=");
            perf_send_u32(s_perf_tx_snap.max_ms);
            hal_uart_send_text(" samples=");
            perf_send_u32(s_perf_tx_snap.samples);
            hal_uart_send_text(" missed=");
            perf_send_u32(s_perf_tx_snap.missed);
            hal_uart_send_text(" bte-hit=");
            perf_send_u32(s_perf_tx_snap.bte_hit);
            hal_uart_send_text(" bte-miss=");
            perf_send_u32(s_perf_tx_snap.bte_miss);
            s_perf_tx_step = 2u;
            break;
        case 2u:
            hal_uart_send_text(" fl=");
            perf_send_u32(s_perf_tx_snap.fl_ms);
            hal_uart_send_text("/");
            perf_send_u32(s_perf_tx_snap.fl_n);
            hal_uart_send_text(" dm=");
            perf_send_u32(s_perf_tx_snap.dm_ms);
            hal_uart_send_text("/");
            perf_send_u32(s_perf_tx_snap.dm_n);
            hal_uart_send_text(" gap=");
            perf_send_u32(s_perf_tx_snap.gap);
            hal_uart_send_text(" jit=");
            perf_send_u32(s_perf_tx_snap.jit_mid);
            hal_uart_send_text("/");
            perf_send_u32(s_perf_tx_snap.jit_big);
            hal_uart_send_text(" input_hz=");
            perf_send_u32(s_perf_tx_snap.fields);
            hal_uart_send_text(" reading_frames=");
            perf_send_u32(s_perf_tx_snap.reading_frames);
            hal_uart_send_text(" display_commits=");
            perf_send_u32(s_perf_tx_snap.commits);
            s_perf_tx_step = 3u;
            break;
        case 3u:
            hal_uart_send_text(" trend_axis_rebuilds=");
            perf_send_u32(s_perf_tx_snap.rebuilds);
            hal_uart_send_text(" trend_column_updates=");
            perf_send_u32(s_perf_tx_snap.columns);
            hal_uart_send_text(" dbg_func_change=");
            perf_send_u32(s_perf_tx_snap.func);
            hal_uart_send_text(" dbg_info_repaint=");
            perf_send_u32(s_perf_tx_snap.info);
            hal_uart_send_text(" dbg_stale_kill=");
            perf_send_u32(s_perf_tx_snap.stale);
            hal_uart_send_text(" dbg_present_hold=");
            perf_send_u32(s_perf_tx_snap.hold);
            hal_uart_send_text(" stg=");
            {
                uint8_t si;
                for (si = 0u; si < 9u; si++)
                {
                    perf_send_u32(s_perf_tx_snap.stg[si]);
                    if (si < 8u)
                        hal_uart_send_text(",");
                }
            }
            s_perf_tx_step = 4u;
            break;
        case 4u:
            hal_uart_send_text(" stgm=");
            {
                uint8_t si;
                for (si = 0u; si < 9u; si++)
                {
                    perf_send_u32(s_perf_tx_snap.stgm[si]);
                    if (si < 8u)
                        hal_uart_send_text(",");
                }
            }
            /* String self-check: resident axis unit + last row-2 range.
             * Bounded sends only (a blocking UART must never run away). */
            {
                const char *p;
                hal_uart_send_text(" axu=");
                p = s_perf_tx_snap.axu;
                while (*p != '\0') { hal_uart_send((const uint8_t *)p, 1u); p++; }
                hal_uart_send_text(" rng=");
                p = s_perf_tx_snap.rng;
                while (*p != '\0') { hal_uart_send((const uint8_t *)p, 1u); p++; }
            }
#if K2000_ROW_DUMP
            {
                /* Late one-shot: the first PERF fires mid virgin-build
                 * (STATUS done, INFO still composing) — useless as ground
                 * truth. Wait for post-rotation steady state. */
                static bool rowdump_done = false;
                if (!rowdump_done && HAL_GetTick() > 30000u)
                {
                    rowdump_done = true;
                    row_debug_dump();
                }
            }
#endif
            hal_uart_send_text(" sw_behind=");
            perf_send_u32(s_perf_tx_snap.sw_behind);
            hal_uart_send_text(" sw_scale=");
            perf_send_u32(s_perf_tx_snap.sw_scale);
            hal_uart_send_text(" sw_reset=");
            perf_send_u32(s_perf_tx_snap.sw_reset);
            hal_uart_send_text(" sw_pxok=");
            perf_send_u32(s_perf_tx_snap.pxok);
            hal_uart_send_text("/");
            perf_send_u32(s_perf_tx_snap.pxmiss);
#if K2000_TREND_DUMP
            trend_debug_dump();
#endif
            hal_uart_send_text(" reading_errors=");
            perf_send_u32(s_perf_tx_snap.reading_errors);
            hal_uart_send_text(" last_error=");
            perf_send_u32(s_perf_tx_snap.last_error);
            hal_uart_send_text("\r\n");
            s_perf_tx_step = 0u;
            break;
        default:
            break;
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
    uint8_t dma_retries;
    bool resolving;
    bool chunk_ready;
    bool active;
    rif_tile_t tile;
    uint8_t entry[RIF_READER_ENTRY_SIZE];
    /* Eight 64-pixel RGB565 rows. Keeping the read contiguous avoids
     * reinitializing the LT7680 Flash Master for every 32-pixel half-row. */
    uint8_t pixels[2048];
} rif_draw_job_t;

static rif_image_t s_rif_image;
static rif_draw_job_t s_rif_draw_job;
static bool s_rif_ready;
static bool s_rif_dma_probe_passed;

#if K2000_READING_ONLY_BASELINE
static void reading_only_abort_frame(lt7680_status_t error)
{
    s_reading_only_render_errors++;
    s_reading_only_last_error = error;
    s_reading_only_dirty = true;
    s_reading_only_stage = READING_ONLY_IDLE;
    s_deferred_row_pending = false;
    s_display_due_tick = HAL_GetTick();
    s_frame_rendering = false;
    s_renderer.phase = RENDER_PHASE_IDLE;
    /* A failed operation may have left only part of a glyph on the target
     * page. Invalidate all page-local coverage so the retry redraws every
     * glyph instead of trusting a partial frame. */
    memset(s_reading_only_page_unit, 0, sizeof(s_reading_only_page_unit));
    memset(s_reading_only_page_suffix, 0, sizeof(s_reading_only_page_suffix));
    memset(s_reading_only_page_value, 0, sizeof(s_reading_only_page_value));
    memset(s_reading_only_page_unit_w, 0,
           sizeof(s_reading_only_page_unit_w));
    memset(s_reading_only_page_suffix_x, 0,
           sizeof(s_reading_only_page_suffix_x));
    memset(s_reading_only_page_value_x, 0,
           sizeof(s_reading_only_page_value_x));
    memset(&s_bitmap_job, 0, sizeof(s_bitmap_job));
    memset(&s_rif_draw_job, 0, sizeof(s_rif_draw_job));
    s_reading_only_io_error = false;
}
#endif

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
    {"VDC", 2u, 5u, 60000u, 120000u, 0x10u, 0x04u},
    {"VAC", 3u, 5u, 30000u, 60000u, 0x30u, 0x02u},
    {"ADC", 2u, 5u, 15000u, 30000u, 0x10u, 0x01u},
    {"AAC", 2u, 5u, 15000u, 30000u, 0x40u, 0x12u},
    {"mVDC", 3u, 5u, 20000u, 40000u, 0x30u, 0x04u},
    {"mVAC", 3u, 5u, 20000u, 40000u, 0x10u, 0x01u},
    {"mADC", 2u, 5u, 20000u, 40000u, 0x50u, 0x04u},
    {"mAAC", 2u, 5u, 20000u, 40000u, 0x30u, 0x02u},
    {"OHM", 4u, 4u, 20000u, 40000u, 0x10u, 0x02u},
    {"kOHM", 3u, 4u, 20000u, 40000u, 0x50u, 0x01u},
    {"MOHM", 3u, 4u, 20000u, 40000u, 0x30u, 0x0Cu},
    {"Hz", 3u, 3u, 20000u, 40000u, 0x10u, 0x04u},
    {"kHz", 3u, 3u, 20000u, 40000u, 0x00u, 0x14u},
    {"MHz", 2u, 3u, 20000u, 40000u, 0x40u, 0x02u},
    {"\xC2\xB0"
     "C",
     2u, 3u, 1000u, 50000u, 0x30u, 0x01u},
};
#define DEMO_UNIT_COUNT \
    ((uint8_t)(sizeof(s_demo_units) / sizeof(s_demo_units[0])))
/* Unit rotation is time-based, not sample-count-based: 20 seconds of
 * samples per unit regardless of input rate. At the default 10 Hz this
 * equals the historical 200 samples; at a 500 Hz test rate it scales to
 * 10000 so the trend buffer is not reset-thrashed by rapid rotation. */
#define DEMO_SAMPLES_PER_UNIT (K2000_DEMO_INPUT_HZ * 20u)

static uint32_t s_demo_last_tick;
static uint32_t s_demo_status_tick;
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
    /* A 30 Hz display frame can occupy nearly 20 ms, which spans ten 500 Hz
     * input ticks. Keep enough catch-up budget to preserve the requested demo
     * rate instead of reporting a synthetic missed sample every frame.
     * 64 covers 25 fps turns (each owes ~20 samples at 500 Hz). */
    uint8_t budget = 64u;

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
        /* Artificial status churn shaping: every flip repaints the rows on
         * both pages (paired stretched frames). The real host drives
         * status rarely; the demo does not need 2 s flips to exercise
         * lamp rendering, so 10 s keeps coverage while staying out of
         * the slow-motion picture. */
        if ((uint16_t)(now - s_demo_status_tick) >= 10000u)
        {
            k2000_proto_feed(K2000_TAG_STATUS_REL);
            k2000_proto_feed(u->status09);
            k2000_proto_feed(K2000_TAG_STATUS_HOLD);
            k2000_proto_feed(u->status08);
            k2000_proto_feed(K2000_TAG_STATUS_SHIFT);
            k2000_proto_feed((unit_index % 4u == 3u) ? 0x20u : 0x00u);
            s_demo_status_tick = now;
        }
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

/* Hard-hang black box. The F1 IWDG cannot interrupt, so a hang produces a
 * reset -- but SRAM survives it. Every healthy loop iteration parks the
 * current renderer context in BKP registers; after an IWDG reset the boot
 * path prints them, turning an unrecoverable freeze into a report. */
#define WDT_IWDG_BASE        0x40003000u
#define WDT_IWDG_KR          (*(volatile uint32_t *)(WDT_IWDG_BASE + 0x00u))
#define WDT_IWDG_PR          (*(volatile uint32_t *)(WDT_IWDG_BASE + 0x04u))
#define WDT_IWDG_RLR         (*(volatile uint32_t *)(WDT_IWDG_BASE + 0x06u))
#define WDT_BKP_DR1          (*(volatile uint16_t *)0x40006C04u)
#define WDT_BKP_DR2          (*(volatile uint16_t *)0x40006C06u)
#define WDT_BKP_DR3          (*(volatile uint16_t *)0x40006C08u)
#define WDT_RCC_CSR          (*(volatile uint32_t *)0x40021050u)
#define WDT_RCC_CSR_IWDGRSTF (1u << 29u)

#define WDT_DBGMCU_APB1FZ (*(volatile uint32_t *)0x40007008u)
#define WDT_DBG_IWDG_STOP (1u << 11u)

__attribute__((unused)) static void wdt_start(void)
{ /* Kept for re-arming once the IWDG-vs-runtime interaction is solved;
   * see the call-site note at the INIT-OK path. */
    /* Freeze the watchdog whenever the core is halted (debug/flash): the
     * IWDG keeps counting across resets AND through debug halts by
     * default, which poisoned every flash/reset session once it was
     * armed. */
    WDT_DBGMCU_APB1FZ |= WDT_DBG_IWDG_STOP;
    WDT_IWDG_KR = 0x5555u;
    /* NOTE: the IWDG keeps counting across system resets -- the whole
     * boot path (LT7680 init, probes, cooperative first frame) must fit
     * inside one timeout or the device boot-loops forever (observed as a
     * dead panel with green vertical stripes). */
    WDT_IWDG_PR = 6u;              /* LSI/256 */
    WDT_IWDG_RLR = 1250u;          /* ~8 s at 40 kHz LSI */
    WDT_IWDG_KR = 0xCCCCu;         /* start */
}

static void wdt_kick(void)
{
    /* Park the live renderer context BEFORE refreshing, so whatever the
     * loop was doing when it wedged is what the post-reset report shows. */
    WDT_BKP_DR1 = 0xCAFEu;
    WDT_BKP_DR2 = (uint16_t)(((uint8_t)s_renderer.phase) |
                         ((uint8_t)s_render_item << 8));
    WDT_BKP_DR3 = (uint16_t)s_render_column;
    WDT_IWDG_KR = 0xAAAAu;
}

static void wdt_report_boot(void)
{
    hal_uart_send_text("[RST] csr=");
    {
        uint32_t csr = WDT_RCC_CSR;
        hal_uart_send_hex8((uint8_t)(csr >> 24));
        hal_uart_send_hex8((uint8_t)(csr >> 16));
        hal_uart_send_hex8((uint8_t)(csr >> 8));
        hal_uart_send_hex8((uint8_t)csr);
    }
    hal_uart_send_text("\r\n");
    if ((WDT_RCC_CSR & WDT_RCC_CSR_IWDGRSTF) == 0u)
        return;
    hal_uart_send_text("[WDT] hang! phase=");
    hal_uart_send_hex8((uint8_t)(WDT_BKP_DR2 & 0xFFu));
    hal_uart_send_text(" item=");
    hal_uart_send_hex8((uint8_t)(WDT_BKP_DR2 >> 8));
    hal_uart_send_text(" col=");
    hal_uart_send_hex8((uint8_t)WDT_BKP_DR3);
    hal_uart_send_text("\r\n");
    WDT_RCC_CSR |= (1u << 24u);    /* RMVF: clear reset flags */
}

static void READING_ONLY_LEGACY display_enable_after_initial_frame(void)
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
        s_perf_display_commits_window++;
        if (!initial_frame && !s_demo_commit_reported)
        {
            hal_uart_send_text("[DEMO] commit page=");
            hal_uart_send_hex8(s_render_page);
            hal_uart_send_text("\r\n");
            s_demo_commit_reported = true;
        }
        s_visible_page = s_render_page;
        s_ready_page_mask |= (uint8_t)(1u << s_render_page);
        /* Runtime composes on one hidden page and levels the sibling with
         * band copies at the next frame start; publish the per-page
         * snapshots to BOTH so next-frame comparisons against either page
         * match and never force a spurious full repaint. */
        s_page_text_generation[0] = s_frame_text_generation;
        s_page_text_generation[1] = s_frame_text_generation;
        if (s_frame_has_trend_update)
        {
            s_page_trend_has_data[0] = s_frame.trend_has_data;
            s_page_trend_has_data[1] = s_frame.trend_has_data;
            s_page_trend_minimum[0] = s_frame.trend_minimum;
            s_page_trend_minimum[1] = s_frame.trend_minimum;
            s_page_trend_maximum[0] = s_frame.trend_maximum;
            s_page_trend_maximum[1] = s_frame.trend_maximum;
        }
        /* Axis identity must be published on EVERY commit: it gates the next
         * frame's rebuild decision. Publishing only on full-page frames left
         * the cache stale forever (runtime frames are never full-page), so
         * the unit comparison fired every frame -- a rebuild storm. Both
         * pages carry identical trend pixels, so one explicit resident
         * axis state replaces the former per-page copies. */
        main_display_get_trend_axis(&s_frame, &s_trend_axis_resident);
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
            s_renderer.trend_resume_at_columns = false;
            s_renderer.initial_complete = true;
            s_renderer.initial_complete_edge = false;
#if K2000_DEMO_FEED
            s_demo_last_tick = HAL_GetTick();
            s_demo_status_tick = HAL_GetTick();
#endif
            if (!initial_complete)
                hal_uart_send_text("PASS frame page enabled\r\n");
            else
                hal_uart_send_text("PASS initial frame enabled\r\n");
        }
    }
}

/* Map a scheduler phase to the UI band its drawing owns. Bands are the
 * unit of inter-page synchronization: a runtime frame only mutates pixels
 * inside the band(s) of its phases, so a band copy can never miss a write. */
static uint8_t frame_region_for_phase(render_phase_t phase)
{
    switch (phase)
    {
    case RENDER_PHASE_INITIAL_STATUS:
    case RENDER_PHASE_UPDATE_STATUS:
        return FRAME_REGION_STATUS;
    case RENDER_PHASE_INITIAL_READING:
    case RENDER_PHASE_UPDATE_READING:
        return FRAME_REGION_READING;
    case RENDER_PHASE_INITIAL_TREND_STATIC:
    case RENDER_PHASE_INITIAL_TREND_COLUMNS:
    case RENDER_PHASE_UPDATE_TREND_AXES:
    case RENDER_PHASE_UPDATE_TREND_COLUMNS:
        return FRAME_REGION_TREND;
    default:
        return 0u;
    }
}

/* Bring the freshly selected hidden page level with the committed page for
 * every band still flagged in s_frame_regions: one BTE rectangle copy per
 * band with full-canvas strides, never a whole-canvas clone. A band bit is
 * cleared only after its copy succeeds, so a bus error retries exactly the
 * remaining work on the next attempt. */
/* UI-x extent actually dirtied inside the reading band this frame. The
 * band's framebuffer image is 168px wide but 960px long; copying only the
 * value/info span instead of the full length roughly halves the per-flip
 * BTE sync cost at 30Hz. Full width until the first stable diff frame. */
static uint16_t s_reading_sync_x0;
static uint16_t s_reading_sync_x1;

static bool hidden_page_sync_regions(void)
{
    static const struct
    {
        uint8_t region;
        uint16_t y;
        uint16_t h;
    } bands[3] = {
        {FRAME_REGION_STATUS, MAIN_DISPLAY_STATUS_Y, MAIN_DISPLAY_STATUS_H},
        {FRAME_REGION_READING, MAIN_DISPLAY_READING_Y, MAIN_DISPLAY_READING_H},
        {FRAME_REGION_TREND, MAIN_DISPLAY_TREND_Y, MAIN_DISPLAY_TREND_H},
    };
    uint8_t i;

    for (i = 0u; i < 3u; i++)
    {
        lt7680_rect_t rect;

        if ((s_frame_regions & bands[i].region) == 0u)
            continue;
        uint16_t band_x0 = 0u;
        uint16_t band_w = MAIN_DISPLAY_UI_WIDTH;

        if (bands[i].region == FRAME_REGION_READING &&
            s_reading_sync_x1 > s_reading_sync_x0)
        {
            band_x0 = s_reading_sync_x0;
            band_w = (uint16_t)(s_reading_sync_x1 - s_reading_sync_x0);
        }
        panel_transform_ui_rect_to_fb(band_x0, bands[i].y, band_w,
                                      bands[i].h, &rect.x, &rect.y,
                                      &rect.w, &rect.h);
        if (rect.w == 0u || rect.h == 0u)
            return false;
        if (lt7680_gfx_copy_rect(s_visible_page, s_render_page, &rect) !=
            LT7680_OK)
            return false;
        /* The copied pixels carry the VISIBLE page's trend state, so the
         * render page's rasterized-column cache must be cloned too --
         * otherwise the column diff compares fresh pixels against this
         * page's stale bookkeeping, skipping redraws and leaving ghost
         * segments of an older curve beside the current one. */
        if (bands[i].region == FRAME_REGION_TREND)
        {
            memcpy(s_drawn_trend_y0[s_render_page],
                   s_drawn_trend_y0[s_visible_page],
                   sizeof(s_drawn_trend_y0[0]));
            memcpy(s_drawn_trend_y1[s_render_page],
                   s_drawn_trend_y1[s_visible_page],
                   sizeof(s_drawn_trend_y1[0]));
            memcpy(s_drawn_trend_occupied[s_render_page],
                   s_drawn_trend_occupied[s_visible_page],
                   sizeof(s_drawn_trend_occupied[0]));
            s_trend_grid_dirty[s_render_page] =
                s_trend_grid_dirty[s_visible_page];
#if K2000_READING_ONLY_BASELINE
            s_reading_only_page_trend_bg_valid[s_render_page] =
                s_reading_only_page_trend_bg_valid[s_visible_page];
            memcpy(s_reading_only_page_trend_unit[s_render_page],
                   s_reading_only_page_trend_unit[s_visible_page],
                   sizeof(s_reading_only_page_trend_unit[0]));
#endif
        }
        s_frame_regions &= (uint8_t)~bands[i].region;
    }
    return true;
}

/* True when normal runtime composition may write the render page only.
 * The blanked first frame builds with dual-page writes so both canvases
 * start pixel-identical; every later frame composes on one page and lets
 * hidden_page_sync_regions() level the sibling at the next frame start.
 * EXCEPTION: the sweep renderer is NOT integrated with the region
 * machinery — its strips are never flagged, so single-page mode leaves
 * the sibling page without the trace and every page flip visibly
 * flickers. While the sweep draws, dual-page writes are forced. */
static bool s_trend_sweep_drawing;
static bool ui_runtime_single_page(void)
{
    return !s_trend_sweep_drawing && s_frame_rendering &&
           !s_render_full_page &&
           frame_region_for_phase(s_renderer.phase) != 0u;
}

/* Page invariant (Task 3, hidden-page rendering):
 *   - At begin_hidden_frame() the composition target s_render_page carries
 *     the last committed scene plus every change not yet committed: the
 *     bands flagged in s_frame_regions are BTE-copied from the visible
 *     page before any new drawing.
 *   - Runtime GE/BTE writes target ONLY s_render_page; initialization and
 *     the blanked first frame keep dual-page writes.
 *   - present_page() atomically makes s_render_page visible, after which
 *     s_visible_page == s_render_page and the sibling page catches up via
 *     the next frame's band copies. */
static bool READING_ONLY_LEGACY begin_hidden_frame(void)
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
        s_frame_regions = 0u;
        st = lt7680_gfx_clear(MAIN_DISPLAY_COLOR_BG);
        if (st != LT7680_OK)
            return false;
        render_scheduler_init(&s_renderer);
        s_waiting_visible = false;
    }
    else
    {
        /* Compose runtime updates on the HIDDEN page and flip atomically at
         * commit. Writing the scanned page makes BTE bursts collide with
         * display fetches (single-pixel sparkles); the hidden page never
         * collides. First replay the previous frames' band deltas so this
         * page starts from the committed scene, then draw on it alone. */
        s_render_page = (uint8_t)(s_visible_page ^ 1u);
        st = lt7680_gfx_select_canvas_page(s_render_page);
        if (st != LT7680_OK)
            return false;
        if (!hidden_page_sync_regions())
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
#if K2000_READING_ONLY_BASELINE
    char previous_trend_unit[TREND_UNIT_ID_MAX];
#endif

    if (evt == 0)
    {
        return;
    }
    switch (evt->type)
    {
    case K2000_EVT_FIELD:
        s_perf_fields_window++;
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
#if K2000_READING_ONLY_BASELINE
        s_reading_only_generation++;
        s_reading_only_dirty = true;
#endif
        if (special == 0u)
        {
#if K2000_READING_ONLY_BASELINE
            strncpy(previous_trend_unit, trend_buffer_display_unit(&s_trend),
                    sizeof(previous_trend_unit) - 1u);
            previous_trend_unit[sizeof(previous_trend_unit) - 1u] = '\0';
#endif
            (void)trend_buffer_add(&s_trend, HAL_GetTick(), num, unit);
#if K2000_READING_ONLY_BASELINE
            if (strcmp(previous_trend_unit, trend_buffer_display_unit(&s_trend)) != 0)
            {
                s_trend_axis_valid = false;
                reading_only_invalidate_trend_pages();
                if (s_trend_pip_ready)
                    trend_pip_reset();
            }
#endif
        }
        s_ui_dirty_regions |= RENDER_DIRTY_READING;
        break;
    case K2000_EVT_STATUS:
        ui_model_apply_status(&s_ui, evt->status_tag, evt->status_value);
        s_ui_dirty_regions |= RENDER_DIRTY_STATUS;
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
    lt7680_status_t st = LT7680_OK;
    uint32_t t0 = HAL_GetTick();
    panel_transform_ui_rect_to_fb(x, y, w, h, &rect.x, &rect.y,
                                  &rect.w, &rect.h);
    if (ui_runtime_single_page())
    {
        st = lt7680_gfx_select_canvas_page(s_render_page);
        if (st == LT7680_OK)
            st = lt7680_gfx_fill_rect(&rect, color);
    }
    else
    {
        uint8_t p;
        for (p = 0u; p < 2u && st == LT7680_OK; p++)
        {
            st = lt7680_gfx_select_canvas_page(p);
            if (st == LT7680_OK)
                st = lt7680_gfx_fill_rect(&rect, color);
        }
    }
    s_prof_fill_ms += HAL_GetTick() - t0;
    s_prof_fill_n++;
#if K2000_READING_ONLY_BASELINE
    if (st != LT7680_OK)
    {
        s_reading_only_last_error = st;
        s_reading_only_io_error = true;
    }
#endif
    return st;
}

static lt7680_status_t ui_draw_line(uint16_t x0, uint16_t y0, uint16_t x1,
                                    uint16_t y1, uint16_t color)
{
    uint16_t fx0, fy0, fx1, fy1;
    lt7680_status_t st = LT7680_OK;
    panel_transform_ui_to_fb(x0, y0, &fx0, &fy0);
    panel_transform_ui_to_fb(x1, y1, &fx1, &fy1);
    if (ui_runtime_single_page())
    {
        st = lt7680_gfx_select_canvas_page(s_render_page);
        if (st == LT7680_OK)
            st = lt7680_gfx_draw_line((int16_t)fx0, (int16_t)fy0,
                                      (int16_t)fx1, (int16_t)fy1, color);
    }
    else
    {
        uint8_t p;
        for (p = 0u; p < 2u && st == LT7680_OK; p++)
        {
            st = lt7680_gfx_select_canvas_page(p);
            if (st == LT7680_OK)
                st = lt7680_gfx_draw_line((int16_t)fx0, (int16_t)fy0,
                                          (int16_t)fx1, (int16_t)fy1, color);
        }
    }
#if K2000_READING_ONLY_BASELINE
    if (st != LT7680_OK)
    {
        s_reading_only_last_error = st;
        s_reading_only_io_error = true;
    }
#endif
    return st;
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
static bool ui_draw_bitmap_slice(bitmap_job_t *job, uint16_t x, uint16_t y,
                                 const char *text, uint16_t color,
                                 uint8_t mode)
{
    /* A GE rectangle is a blocking SPI transaction.  Keep this small enough
     * that the main loop can return to the RX ISR/keypad between calls; a
     * large glyph may therefore span several cooperative calls. */
    uint16_t budget = (mode == 3u || mode == 4u) ? 256u : 64u;
    if (!job->active)
    {
        job->text = text;
        job->x = x;
        job->y = y;
        job->color = color;
        job->cx = x;
        job->row = 0u;
        job->col = 0u;
        job->mode = mode;
        job->active = true;
    }
    while (budget > 0u && *job->text != '\0')
    {
        uint8_t advance = 1u;
        uint8_t width = FONT_TEXT_WIDTH;
        uint8_t height = FONT_TEXT_HEIGHT;
        uint8_t bpr = FONT_TEXT_BYTES_PER_ROW;
        const uint8_t *bitmap;
        if (job->mode == 2u)
        {
            bitmap = font_half_bitmap(*s_bitmap_job.text);
            width = FONT_HALF_WIDTH;
            height = FONT_HALF_HEIGHT;
            bpr = FONT_HALF_BYTES_PER_ROW;
        }
        else
        {
            bitmap = text_glyph(job->text, &advance);
        }
        if (bitmap == 0)
        {
            job->text += advance;
            continue;
        }
        while (job->row < height)
        {
            const uint8_t *bits = bitmap + job->row * bpr;
            while (job->col < width &&
                   (bits[job->col >> 3] & (uint8_t)(0x80u >> (job->col & 7u))) == 0u)
                job->col++;
            if (job->col < width)
            {
                uint8_t start = job->col;
                while (job->col < width &&
                       (bits[job->col >> 3] & (uint8_t)(0x80u >> (job->col & 7u))) != 0u)
                    job->col++;
                if (ui_fill_rect(
                         (uint16_t)(job->cx + start),
                         (uint16_t)(job->y + (mode == 4u ? job->row / 2u : job->row)),
                        (uint16_t)(job->col - start), 1u,
                        job->color) != LT7680_OK)
                {
                    job->active = false;
                    return false;
                }
                budget--;
                if (budget == 0u)
                    return false;
            }
            else
            {
                job->col = 0u;
                job->row++;
            }
        }
        job->row = 0u;
        job->col = 0u;
        job->cx = (uint16_t)(job->cx + width);
        job->text += advance;
    }
    job->active = false;
    return true;
}

static bool ui_draw_text(uint16_t x, uint16_t y, const char *text,
                         uint16_t color)
{
    return ui_draw_bitmap_slice(&s_bitmap_job, x, y, text, color, 0u);
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
#if K2000_READING_ONLY_BASELINE
    s_reading_only_last_error = st != LT7680_OK ? st : LT7680_ERR_BUS;
    s_reading_only_io_error = true;
#endif
    s_rif_ready = false;
#if RIF_BTE_RENDERER
    s_cell_count = 0u; /* canvas identity no longer matches the cell table */
#endif
    hal_uart_send_text("RIF fallback=");
    hal_uart_send_hex8((uint8_t)st);
    hal_uart_send_text("\r\n");
}

/* RAM-resident directory of every glyph resolved during prebuild. Scanning
 * the Flash directory per glyph dominated the frame budget (~370 ms/frame);
 * this table makes resolution an O(1) RAM lookup. */
typedef struct
{
    uint32_t kind;
    uint16_t code;
    rif_tile_t tile;
    uint32_t sdram;
} rif_dir_entry_t;

#define RIF_DIR_CACHE_MAX 40u
/* Glyph tiles streamed per-draw through the serial-flash SPI FIFO cost
 * ~4.6 ms each; at a 500 Hz reading stream that stalled every commit.
 * At boot the whole directory is staged once into spare SDRAM located
 * INSIDE the page-1 slot above the scanned 320x960 image (offset
 * 0xA0000 > 0x96000 used by the panel fetch, GE fills and band copies,
 * all of which are bounded by the canvas geometry). Render-time glyphs
 * are then engine-copied from SDRAM via the verified lt7680_gfx_blit. */
#define RIF_SDRAM_CACHE_BASE 0x00200000u
static rif_dir_entry_t s_dir_cache[RIF_DIR_CACHE_MAX];
static uint8_t s_dir_cache_count;
static bool s_glyph_cache_ready;

static const rif_dir_entry_t *rif_dir_find(uint32_t kind, uint16_t code)
{
    uint8_t i;
    for (i = 0u; i < s_dir_cache_count; i++)
    {
        if (s_dir_cache[i].kind == kind && s_dir_cache[i].code == code)
            return &s_dir_cache[i];
    }
    return 0;
}

static bool rif_dir_lookup(uint32_t kind, uint16_t code, rif_tile_t *tile)
{
    uint8_t i;

    for (i = 0u; i < s_dir_cache_count; i++)
    {
        if (s_dir_cache[i].kind == kind && s_dir_cache[i].code == code)
        {
            *tile = s_dir_cache[i].tile;
            return true;
        }
    }
    return false;
}

static void rif_dir_remember(uint32_t kind, uint16_t code,
                             const rif_tile_t *tile)
{
    if (s_dir_cache_count >= RIF_DIR_CACHE_MAX)
        return;
    s_dir_cache[s_dir_cache_count].kind = kind;
    s_dir_cache[s_dir_cache_count].code = code;
    s_dir_cache[s_dir_cache_count].tile = *tile;
    s_dir_cache_count++;
}

static bool rif_find_next_tile(void)
{
    rif_entry_t entry;
    lt7680_status_t st;

    if (rif_dir_lookup(s_rif_draw_job.kind, s_rif_draw_job.code,
                       &s_rif_draw_job.tile))
    {
        s_rif_draw_job.resolving = false;
        s_rif_draw_job.row = 0u;
        s_rif_draw_job.column = 0u;
        return false;
    }

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
    /* Vendor glyph path: tiles are stored PRE-TRANSPOSED in U5 (128x64,
     * stride 256) and drawn with a single block DMA flash->canvas. Colors
     * are baked in; only the background must match the canvas. Any other
     * geometry or color falls through to the run-length renderer below. */
    if (color == MAIN_DISPLAY_COLOR_GREEN &&
        s_rif_draw_job.tile.background == MAIN_DISPLAY_COLOR_BG &&
        s_rif_draw_job.tile.width == FONT_DIGIT_HEIGHT &&
        s_rif_draw_job.tile.height == FONT_DIGIT_WIDTH &&
        s_rif_draw_job.tile.stride == FONT_DIGIT_HEIGHT * 2u)
    if (color == MAIN_DISPLAY_COLOR_GREEN &&
        s_rif_draw_job.tile.background == MAIN_DISPLAY_COLOR_BG &&
        s_rif_draw_job.tile.width == FONT_DIGIT_HEIGHT &&
        s_rif_draw_job.tile.height == FONT_DIGIT_WIDTH &&
        s_rif_draw_job.tile.stride == FONT_DIGIT_HEIGHT * 2u)
    {
        uint16_t fb_x;
        uint16_t fb_y;

        {
            rif_cell_t *cell = NULL;
            panel_transform_ui_to_fb(s_rif_draw_job.cx, s_rif_draw_job.y,
                                     &fb_x, &fb_y);
            /* Diff mode: a cell whose glyph identity is unchanged already
             * sits on both canvases pixel-for-pixel; skip entirely. */
            if (s_reading_diff)
                cell = rif_cell_find(s_rif_draw_job.cx,
                                     s_rif_draw_job.y,
                                     s_rif_draw_job.kind,
                                     s_rif_draw_job.code);
            if (cell != NULL && cell->fresh == 0u)
            {
                s_rif_bte_hits++;
                s_rif_draw_job.cx = (uint16_t)(s_rif_draw_job.cx +
                                               FONT_DIGIT_WIDTH);
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
            if (cell != NULL)
                cell->fresh = 0u;

            if ((uint32_t)fb_x + FONT_DIGIT_HEIGHT <= MAIN_DISPLAY_UI_HEIGHT &&
                (uint32_t)fb_y + FONT_DIGIT_WIDTH <= MAIN_DISPLAY_UI_WIDTH)
            {
                uint32_t t0 = HAL_GetTick();
                const rif_dir_entry_t *cached =
                    s_glyph_cache_ready
                        ? rif_dir_find(s_rif_draw_job.kind,
                                       s_rif_draw_job.code)
                        : 0;
                st = LT7680_OK;
                if (cached != 0 && !READING_ONLY_DIRECT_DMA)
                {
                    if (ui_runtime_single_page())
                    {
                        st = lt7680_gfx_blit(
                            s_render_page, cached->sdram,
                            (uint16_t)(cached->tile.stride / 2u),
                            fb_x, fb_y,
                            FONT_DIGIT_HEIGHT, FONT_DIGIT_WIDTH);
                    }
                    else
                    for (uint8_t pg = 0u; pg < 2u && st == LT7680_OK; pg++)
                    {
                        st = lt7680_gfx_blit(
                            (uint8_t)pg, cached->sdram,
                            (uint16_t)(cached->tile.stride / 2u),
                            fb_x, fb_y,
                            FONT_DIGIT_HEIGHT, FONT_DIGIT_WIDTH);
                    }
                }
                else if (ui_runtime_single_page())
                {
                    st = lt7680_flash_dma_tile_to_canvas(
                        s_rif_draw_job.tile.offset,
                        (uint32_t)s_render_page * 0x100000u, 320u,
                        fb_x, fb_y,
                        FONT_DIGIT_HEIGHT, FONT_DIGIT_WIDTH);
                }
                else
                for (uint8_t pg = 0u; pg < 2u && st == LT7680_OK; pg++)
                {
                    st = lt7680_flash_dma_tile_to_canvas(
                        s_rif_draw_job.tile.offset,
                        (uint32_t)pg * 0x100000u, 320u,
                        fb_x, fb_y,
                        FONT_DIGIT_HEIGHT, FONT_DIGIT_WIDTH);
                }
                s_prof_dma_ms += HAL_GetTick() - t0;
                s_prof_dma_n++;
                if (st != LT7680_OK)
                {
                    s_reading_only_last_error = st;
                    s_reading_only_io_error = true;
                    /* Never fall through to the run-length renderer here:
                     * U5 tiles are pre-transposed and its UI-space drawing
                     * would smear them across the reading band. Retry the
                     * same cell on later slices; after three failures skip
                     * the glyph so the frame can still commit. */
                    if (s_rif_draw_job.dma_retries < 200u)
                        s_rif_draw_job.dma_retries++;
                    if (s_rif_draw_job.dma_retries >= 3u)
                    {
                        s_rif_draw_job.dma_retries = 0u;
                        s_rif_draw_job.cx = (uint16_t)(s_rif_draw_job.cx +
                                                       FONT_DIGIT_WIDTH);
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
                    }
                    return false;
                }
                s_rif_draw_job.dma_retries = 0u;
                if (st == LT7680_OK)
                {
                    s_rif_bte_hits++;
                    s_rif_draw_job.cx = (uint16_t)(s_rif_draw_job.cx +
                                                   FONT_DIGIT_WIDTH);
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
        /* Hard gate: on a pre-transposed image the run-length renderer
         * cannot draw digit tiles correctly (they are framebuffer-
         * oriented). Skip the glyph instead of smearing it. */
        if (s_rif_draw_job.kind == RIF_KIND_DIGIT_CHAR ||
            s_rif_draw_job.kind == RIF_KIND_DIGIT_SYMBOL)
        {
            s_rif_draw_job.cx = (uint16_t)(s_rif_draw_job.cx +
                                           FONT_DIGIT_WIDTH);
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

static bool rif_find_tile_kind(uint32_t kind, uint16_t code, rif_tile_t *tile);

static bool rif_find_tile_char(uint16_t code, rif_tile_t *tile)
{
    return rif_find_tile_kind(RIF_KIND_DIGIT_CHAR, code, tile);
}

static bool rif_find_tile_kind(uint32_t kind, uint16_t code, rif_tile_t *tile)
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
        if (rif_reader_find_glyph(&s_rif_image, &entry, kind,
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
    rif_tile_t tile = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
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
        tile.width != 0u && tile.height != 0u &&
        tile.size == (uint32_t)tile.width * tile.height * 2u)
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

    s_rif_dma_probe_passed = dma_status == LT7680_OK;
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
    s_dir_cache_count = 0u;
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
    /* The BTE probe currently reports command completion only; until its
     * pixels are independently accepted, do not present its diagnostic page
     * during normal boot. */
    if (s_rif_dma_probe_passed)
    {
        /* Glyphs draw straight from U5 via block DMA -- no SDRAM cache to
         * build. Just walk the directory once into the RAM lookup table so
         * per-glyph resolution is O(1) at render time. */
        static const char cache_chars[] =
            "0123456789.+-Ee%mukKMWVOhDAC?RFLHzs";
        uint16_t char_index;

        for (char_index = 0u; cache_chars[char_index] != '\0'; char_index++)
        {
            rif_tile_t cache_tile;

            if (!rif_find_tile_char((uint16_t)cache_chars[char_index],
                                    &cache_tile))
                continue;
            rif_dir_remember(RIF_KIND_DIGIT_CHAR,
                             (uint16_t)cache_chars[char_index], &cache_tile);
        }
        for (uint8_t sym = 0u; sym < FONT_DIGIT_SYM_COUNT; sym++)
        {
            rif_tile_t cache_tile;

            if (!rif_find_tile_kind(RIF_KIND_DIGIT_SYMBOL, sym, &cache_tile))
                continue;
            rif_dir_remember(RIF_KIND_DIGIT_SYMBOL, sym, &cache_tile);
        }
        /* One-time bulk stage flash -> SDRAM. Fails soft: without the
         * cache the draw path falls back to per-glyph flash DMA. */
        {
            uint8_t i;
            uint32_t next = RIF_SDRAM_CACHE_BASE;

            /* Stage with the VENDOR GLYPH DMA path -- the same verified
             * lt7680_flash_dma_tile_to_canvas() used at runtime -- pointed
             * at a tiny scratch canvas per tile. One whole-tile transfer,
             * no manual strip/slice stitching: the flash-DMA-to-canvas
             * engine rejects wide rows and its strip addressing could not
             * be reconciled with the packed layout, while this path draws
             * 104x56 tiles correctly every frame already. */
            s_glyph_cache_ready = true;
            for (i = 0u; i < s_dir_cache_count; i++)
            {
                rif_tile_t *t = &s_dir_cache[i].tile;

                if (lt7680_flash_dma_tile_to_canvas(
                        t->offset, next,
                        (uint16_t)(t->stride / 2u),
                        0u, 0u, t->width, t->height) != LT7680_OK)
                {
                    s_glyph_cache_ready = false;
                    break;
                }
                s_dir_cache[i].sdram = next;
                next += ((uint32_t)t->stride * t->height + 3u) & ~3u;
            }
            hal_uart_send_text("RIF sdram glyph cache=");
            hal_uart_send_text(s_glyph_cache_ready ? "OK " : "FAIL ");
            hal_uart_send_hex8(s_dir_cache_count);
            hal_uart_send_text(" tiles\r\n");
        }
        /* Pixel probe LAST: its canvas-base/width switching was measured to
         * wedge every subsequent flash-DMA (timeout on an address that had
         * just staged fine), so the glyph cache must be populated first. */
        {
            bool cache_pixel_ok = rif_cache_pixel_probe();
            s_rif_dma_probe_passed = s_rif_dma_probe_passed && cache_pixel_ok;
        }
        /* Validate on-screen geometry against what the active renderer
         * expects. A mismatched image (e.g. packed without --transpose)
         * used to fall into the run-length renderer, which advances x by
         * tile.width and smears the reading across the info panel. */
        {
            rif_tile_t probe;
            if (rif_find_tile_char((uint16_t)'0', &probe))
            {
                hal_uart_send_text("RIF geom0 w=");
                hal_uart_send_hex8((uint8_t)probe.width);
                hal_uart_send_text(" h=");
                hal_uart_send_hex8((uint8_t)probe.height);
                hal_uart_send_text(" stride=0x");
                hal_uart_send_hex8((uint8_t)(probe.stride >> 8));
                hal_uart_send_hex8((uint8_t)(probe.stride & 0xFFu));
                hal_uart_send_text("\r\n");
#if RIF_BTE_RENDERER
                if (probe.width != FONT_DIGIT_HEIGHT ||
                    probe.height != FONT_DIGIT_WIDTH ||
                    probe.stride != (uint16_t)(FONT_DIGIT_HEIGHT * 2u))
#else
                if (probe.width != FONT_DIGIT_WIDTH ||
                    probe.height != FONT_DIGIT_HEIGHT ||
                    probe.stride != (uint16_t)(FONT_DIGIT_WIDTH * 2u))
#endif
                {
                    hal_uart_send_text("FAIL rif-geom mismatch: "
                                       "disable external digits\r\n");
                    s_rif_dma_probe_passed = false;
                    s_rif_ready = false;
                }
            }
        }
        if (s_rif_dma_probe_passed)
            hal_uart_send_text("RIF glyph dir ready\r\n");
    }
#if RIF_BTE_RENDERER
    hal_uart_send_text("RIF BTE renderer=");
    hal_uart_send_text(s_rif_dma_probe_passed ? "ON\r\n" : "OFF\r\n");
#else
    hal_uart_send_text("RIF BTE renderer=OFF (compile)\r\n");
#endif
    hal_uart_send_text("RIF external digits ready\r\n");
}

static bool READING_ONLY_LEGACY ui_draw_half(uint16_t x, uint16_t y, const char *text,
                         uint16_t color)
{
    return ui_draw_bitmap_slice(&s_bitmap_job, x, y, text, color, 2u);
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
static bool READING_ONLY_LEGACY reading_draw_info(uint8_t n)
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
    if ((s_status_info_dirty_rows & (uint8_t)(1u << row)) == 0u)
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
        {
            /* The three lamps form one resumable sequence. An interrupted
             * slice must resume the INTERRUPTED string: restarting from
             * "FILT" would complete the still-active job, then re-init and
             * redraw the finished strings every slice, starving the last
             * lamp's budget forever (item never advances, frame never
             * commits). */
            static const char *const lamps[3] = {"FILT", "REL", "MATH"};
            static const uint8_t lamp_bits[3] = {7u, 6u, 11u};
            static uint8_t lamp_index;
            for (;;)
            {
                uint16_t lx =
                    (uint16_t)(vx + (uint16_t)lamp_index *
                                        4u * FONT_TEXT_WIDTH);
                uint16_t lc =
                    s_frame.status_active[lamp_bits[lamp_index]]
                        ? MAIN_DISPLAY_COLOR_GREEN
                        : MAIN_DISPLAY_COLOR_MUTED;
                if (!ui_draw_text(lx, ty, lamps[lamp_index], lc))
                    return false;
                lamp_index++;
                if (lamp_index >= 3u)
                {
                    lamp_index = 0u;
                    return true;
                }
            }
        }
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
                                i * MAIN_DISPLAY_PLOT_H / 2u);
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

/* ADR-0006: baseline draws no axis labels; kept for the legacy renderer. */
static uint16_t READING_ONLY_LEGACY trend_y_label_y(uint8_t index)
{
    uint16_t axis_y = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                 index * MAIN_DISPLAY_PLOT_H / 2u);
    uint16_t label_y = axis_y > FONT_TEXT_HEIGHT / 2u
                           ? (uint16_t)(axis_y - FONT_TEXT_HEIGHT / 2u)
                           : 0u;

    if (index + 1u == MAIN_DISPLAY_Y_LABEL_COUNT)
        label_y = (uint16_t)(MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H -
                             FONT_TEXT_HEIGHT);

    /* The first label is centred near the top grid line, but its bitmap must
     * remain inside the resident trend region. Otherwise a periodic axis
     * refresh erases the bottom of the reading band at y=188..191. */
    return label_y < MAIN_DISPLAY_TREND_Y ? MAIN_DISPLAY_TREND_Y : label_y;
}

static bool READING_ONLY_LEGACY trend_draw_column(uint16_t column, bool erase_previous)
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
        y0 = main_display_trend_plot_y(c->maximum, s_frame.trend_minimum,
                                       s_frame.trend_maximum);
        y1 = main_display_trend_plot_y(c->minimum, s_frame.trend_minimum,
                                       s_frame.trend_maximum);
        if (y1 < y0)
        {
            uint8_t tmp = y0;
            y0 = y1;
            y1 = tmp;
        }
    }
    if (occupied == trend_drawn_occupied(column) &&
        (!occupied ||
         ((uint8_t)(s_drawn_trend_y0[s_render_page][column] > y0
               ? s_drawn_trend_y0[s_render_page][column] - y0
                : y0 - s_drawn_trend_y0[s_render_page][column]) == 0u &&
          (uint8_t)(s_drawn_trend_y1[s_render_page][column] > y1
               ? s_drawn_trend_y1[s_render_page][column] - y1
                : y1 - s_drawn_trend_y1[s_render_page][column]) == 0u)))
    {
        return true;
    }

    if (erase_previous && trend_drawn_occupied(column))
    {
        uint16_t old_y0 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     s_drawn_trend_y0[s_render_page][column]);
        uint16_t old_y1 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     s_drawn_trend_y1[s_render_page][column]);
        if (ui_fill_rect(x0, old_y0, (uint16_t)(x1 - x0 + 1u),
                         (uint16_t)(old_y1 - old_y0 + 1u),
                         MAIN_DISPLAY_COLOR_BG) != LT7680_OK)
            return false;
        s_trend_grid_dirty[s_render_page] = true;
    }
    if (occupied)
    {
        if (ui_draw_line(x, (uint16_t)(MAIN_DISPLAY_PLOT_Y + y0), x,
                         (uint16_t)(MAIN_DISPLAY_PLOT_Y + y1),
                         MAIN_DISPLAY_COLOR_GREEN) != LT7680_OK)
            return false;
    }
    trend_set_drawn(column, occupied, y0, y1);
    s_perf_trend_columns_window++;
    return true;
}

static bool READING_ONLY_LEGACY trend_join_column(uint16_t column)
{
    uint16_t x0;
    uint16_t x1;
    uint16_t y0;
    uint16_t y1;

    if (column == 0u || !trend_drawn_occupied((uint16_t)(column - 1u)) ||
        !trend_drawn_occupied(column))
        return true;
    x0 = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                    (uint32_t)(column - 1u) * MAIN_DISPLAY_PLOT_W /
                        TREND_MAX_COLUMNS);
    x1 = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                    (uint32_t)column * MAIN_DISPLAY_PLOT_W /
                        TREND_MAX_COLUMNS);
    y0 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                    (s_drawn_trend_y0[s_render_page][column - 1u] +
                     s_drawn_trend_y1[s_render_page][column - 1u]) /
                        2u);
    y1 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                    (s_drawn_trend_y0[s_render_page][column] +
                     s_drawn_trend_y1[s_render_page][column]) /
                        2u);
    return ui_draw_line(x0, y0, x1, y1, MAIN_DISPLAY_COLOR_GREEN) ==
           LT7680_OK;
}

/* Sweep renderer (2026-08-30). This die's BTE blit corrupts pixels and its
 * PIP windows never composite, so hardware "scroll" is unavailable. Classic
 * oscilloscope sweep instead: absolute time maps to fixed screen columns,
 * new samples overwrite in place, nothing is ever moved. Data source is the
 * 20 ms bucket ring inside trend_buffer_t; one render pass advances the
 * cursor by a bounded bucket budget so a pass always fits the frame.
 * Slot addressing is CYCLE-relative: the buffer keeps the last
 * TREND_BUCKET_COUNT buckets, so a slot at the cursor resolves to the
 * buckets in the cursor's own sweep cycle, not the very first one. */
/* Rescan budget: a 48-bucket catch-up visit burns ~200 ms in one turn
 * (dual erase+draw+restore per slot) and shows up as a frozen reading.
 * 16 spreads the same catch-up over a few frames (cursor persists);
 * refill after rotation takes ~1 s instead of one long hitch. */
#define TREND_SWEEP_BUDGET 8u
/* Post-rotation drain: 32 slots/visit cost ~400+ ms of TREND stage per
 * gear-change window (fps dips to single digits). 16 halves the per-visit
 * cost while drain (160 slots/s) still outruns arrivals (25 buckets/s at
 * 500 Hz input); the visible catch-up takes a few more frames instead of
 * one long hitch. Steady-state visits still use TREND_SWEEP_BUDGET. */
#define TREND_SWEEP_RESCAN_BUDGET 16u
static uint16_t trend_grid_x(uint8_t gi);
static bool trend_sweep_restore_verticals(uint16_t x0, uint16_t x1);
static uint32_t s_sweep_epoch_bucket;
/* s_sweep_cursor_bucket declared with the perf counters above. */
static uint32_t s_sweep_epoch_first_ms; /* trend_buffer reset detector */
static float s_sweep_scale_lo, s_sweep_scale_hi; /* applied axis, jitter gate */
static bool s_sweep_active;
static uint32_t s_sweep_cycle; /* completed 500-bucket cycles at last render */

/* One-shot wipe when the sweep wraps: the previous cycle's trace would
 * otherwise linger on the right of the cursor for a full 10 s window.
 * Classic scope behavior is to clear the plot at the start of a new
 * sweep, so erase the plot area and forget per-slot bookkeeping.
 * ADR-0006: no grid to restore — the plot is bare black. */
static void trend_sweep_wipe_cycle(void)
{
    uint8_t page;

    (void)ui_fill_rect(MAIN_DISPLAY_PLOT_X, MAIN_DISPLAY_PLOT_Y,
                       MAIN_DISPLAY_PLOT_W, MAIN_DISPLAY_PLOT_H,
                       MAIN_DISPLAY_COLOR_BG);
    {
        uint8_t gi;
        for (gi = 0u; gi < MAIN_DISPLAY_TREND_GRID_COUNT; gi++)
            (void)ui_draw_line(trend_grid_x(gi), MAIN_DISPLAY_PLOT_Y,
                               trend_grid_x(gi),
                               (uint16_t)(MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H - 1u),
                               MAIN_DISPLAY_COLOR_GRID);
    }
    for (page = 0u; page < 2u; page++)
    {
        memset(s_drawn_trend_y0[page], 0, sizeof(s_drawn_trend_y0[page]));
        memset(s_drawn_trend_y1[page], 0, sizeof(s_drawn_trend_y1[page]));
        memset(s_drawn_trend_occupied[page], 0,
               sizeof(s_drawn_trend_occupied[page]));
    }
}

static uint16_t trend_sweep_slot_of_bucket(uint32_t bucket)
{
    return (uint16_t)(((bucket - s_sweep_epoch_bucket) * TREND_MAX_COLUMNS /
                       TREND_BUCKET_COUNT) %
                      TREND_MAX_COLUMNS);
}

/* Bucket window [w0,w1) that `slot` maps to inside the cycle containing
 * `ref_bucket`. Each cycle spans TREND_BUCKET_COUNT buckets. */
static void trend_sweep_slot_window(uint16_t slot, uint32_t ref_bucket,
                                    uint32_t *w0, uint32_t *w1)
{
    uint32_t cycle_base =
        s_sweep_epoch_bucket +
        ((ref_bucket - s_sweep_epoch_bucket) / TREND_BUCKET_COUNT) *
            TREND_BUCKET_COUNT;

    *w0 = cycle_base +
          ((uint32_t)slot * TREND_BUCKET_COUNT) / TREND_MAX_COLUMNS;
    *w1 = cycle_base +
          ((uint32_t)(slot + 1u) * TREND_BUCKET_COUNT) / TREND_MAX_COLUMNS;
}

static bool trend_sweep_bucket_range(uint32_t bucket, float *lo, float *hi)
{
    uint16_t idx = (uint16_t)(bucket % TREND_BUCKET_COUNT);

    if (bucket > s_trend.newest_bucket ||
        (s_trend.newest_bucket - bucket) >= TREND_BUCKET_COUNT)
        return false;
    if (((s_trend.occupied[idx >> 3] >> (idx & 7u)) & 1u) == 0u)
        return false;
    *lo = s_trend.minimum[idx];
    *hi = s_trend.maximum[idx];
    return true;
}

static void trend_sweep_mirror_drawn(uint16_t slot)
{
    /* GE primitives write both pages, so both pages hold identical pixels
     * for this slot; the per-page drawn bookkeeping must say so too. The
     * alternate page would otherwise re-render the same slot on the next
     * frame flip, visibly jittering the trace left and right. */
    uint8_t page;
    for (page = 0u; page < 2u; page++)
    {
        uint8_t mask = (uint8_t)(1u << (slot & 7u));
        if (((s_drawn_trend_occupied[page][slot >> 3] & mask) != 0u) ==
            ((s_drawn_trend_occupied[page ^ 1u][slot >> 3] & mask) != 0u) &&
            s_drawn_trend_y0[page][slot] == s_drawn_trend_y0[page ^ 1u][slot] &&
            s_drawn_trend_y1[page][slot] == s_drawn_trend_y1[page ^ 1u][slot])
            continue;
        s_drawn_trend_occupied[page ^ 1u][slot >> 3] =
            (uint8_t)((s_drawn_trend_occupied[page ^ 1u][slot >> 3] &
                       (uint8_t)~mask) |
                      (s_drawn_trend_occupied[page][slot >> 3] & mask));
        s_drawn_trend_y0[page ^ 1u][slot] = s_drawn_trend_y0[page][slot];
        s_drawn_trend_y1[page ^ 1u][slot] = s_drawn_trend_y1[page][slot];
    }
}

/* Slots without a sample HOLD the previous value as a short flat segment
 * (staircase), which is both honest for instrument readings and keeps
 * every slot's pixels inside its own 3 px strip: a diagonal join would
 * leave line stubs in neighbouring strips when the later slot is next
 * erased. The chain propagates because each empty slot is rendered once,
 * left to right, behind the slot it holds from. */
static bool trend_sweep_hold_prev(uint16_t slot, uint8_t *y0, uint8_t *y1)
{
    uint16_t p = (uint16_t)((slot + TREND_MAX_COLUMNS - 1u) %
                            TREND_MAX_COLUMNS);

    if (!trend_drawn_occupied(p))
        return false;
    *y0 = (uint8_t)((s_drawn_trend_y0[s_render_page][p] +
                     s_drawn_trend_y1[s_render_page][p]) / 2u);
    *y1 = *y0;
    return true;
}

static bool trend_sweep_render_slot(uint16_t slot, uint32_t ref_bucket)
{
    uint32_t b0, b1;
    uint32_t b;
    float lo = 0.0f, hi = 0.0f;
    bool occ = false;
    uint16_t x = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                            (uint32_t)slot * MAIN_DISPLAY_PLOT_W /
                                TREND_MAX_COLUMNS);
    uint16_t x0 = x > MAIN_DISPLAY_PLOT_X ? (uint16_t)(x - 1u) : x;
    uint16_t x1 = x < (uint16_t)(MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W -
                                1u)
                      ? (uint16_t)(x + 1u)
                      : x;
    uint8_t y0 = 0u, y1 = 0u;
    bool drawn = trend_drawn_occupied(slot);

    trend_sweep_slot_window(slot, ref_bucket, &b0, &b1);
    for (b = b0; b < b1; b++)
    {
        float blo, bhi;
        if (trend_sweep_bucket_range(b, &blo, &bhi))
        {
            if (!occ)
            {
                lo = blo;
                hi = bhi;
                occ = true;
            }
            else
            {
                if (blo < lo) lo = blo;
                if (bhi > hi) hi = bhi;
            }
        }
    }
    if (occ && s_frame.trend_has_data)
    {
        y0 = main_display_trend_plot_y(hi, s_frame.trend_minimum,
                                       s_frame.trend_maximum);
        y1 = main_display_trend_plot_y(lo, s_frame.trend_minimum,
                                       s_frame.trend_maximum);
        if (y1 < y0)
        {
            uint8_t tmp = y0;
            y0 = y1;
            y1 = tmp;
        }
    }
    else
    {
        /* No sample in this slot: hold the previous value so the trace
         * stays continuous across the sample gap. */
        uint8_t hy0 = 0u, hy1 = 0u;

        if (trend_sweep_hold_prev(slot, &hy0, &hy1))
        {
            y0 = hy0;
            y1 = hy1;
            occ = true;
        }
        else
        {
            occ = false;
        }
    }

    if (occ == drawn &&
        (!occ || ((uint8_t)(s_drawn_trend_y0[s_render_page][slot] > y0
                           ? s_drawn_trend_y0[s_render_page][slot] - y0
                           : y0 - s_drawn_trend_y0[s_render_page][slot]) ==
                      0u &&
                  (uint8_t)(s_drawn_trend_y1[s_render_page][slot] > y1
                           ? s_drawn_trend_y1[s_render_page][slot] - y1
                           : y1 - s_drawn_trend_y1[s_render_page][slot]) ==
                      0u)))
        return true;

    if (drawn)
    {
        uint16_t old_y0 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     s_drawn_trend_y0[s_render_page][slot]);
        uint16_t old_y1 = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     s_drawn_trend_y1[s_render_page][slot]);
        if (ui_fill_rect(x0, old_y0, (uint16_t)(x1 - x0 + 1u),
                         (uint16_t)(old_y1 - old_y0 + 1u),
                         MAIN_DISPLAY_COLOR_BG) != LT7680_OK)
            return false;
        /* ADR-0007: the erase punches holes in the vertical indicator
         * lines — restore the ones crossing this strip. */
        if (!trend_sweep_restore_verticals(x0, (uint16_t)(x1 + 1u)))
            return false;
    }
    if (occ)
    {
        if (ui_draw_line(x, (uint16_t)(MAIN_DISPLAY_PLOT_Y + y0), x,
                         (uint16_t)(MAIN_DISPLAY_PLOT_Y + y1),
                         MAIN_DISPLAY_COLOR_GREEN) != LT7680_OK)
            return false;
    }
    trend_set_drawn(slot, occ, y0, y1);
    trend_sweep_mirror_drawn(slot);
    s_perf_trend_columns_window++;
    /* Pixel-landing probe (diagnostic only, default OFF): each peek costs
     * page selects + width sets + a slow MRWDP read, and at 500 Hz the
     * storm alone visibly stalls range-change rebuilds. */
#if TREND_SWEEP_PROBE
    {
        /* Reconciliation probe: did the pixels actually land? Read the
         * slot center back from the visible page. */
        uint16_t fx, fy, px = 0u;
        uint16_t py = (uint16_t)(occ ? (MAIN_DISPLAY_PLOT_Y +
                                       (y0 + y1) / 2u)
                                     : MAIN_DISPLAY_PLOT_Y + 45u);

        if (occ)
        {
            (void)lt7680_gfx_select_canvas_page(s_visible_page);
            (void)lt7680_gfx_set_canvas_width(320u);
            panel_transform_ui_to_fb(x, py, &fx, &fy);
            if (lt7680_gfx_peek_pixel(fx, fy, &px) == LT7680_OK &&
                px != MAIN_DISPLAY_COLOR_GREEN &&
                px != MAIN_DISPLAY_COLOR_GREEN_DIM)
            {
                s_sweep_px_missed++;
            }
            else
            {
                s_sweep_px_ok++;
            }
            (void)lt7680_gfx_select_canvas_page(s_render_page);
            (void)lt7680_gfx_set_canvas_width(320u);
        }
    }
#endif
    return true;
}

static bool trend_sweep_advance(void)
{
    uint32_t target = s_trend.newest_bucket;
    uint32_t steps;
    uint32_t budget;
    uint32_t i;

    if (!s_trend.has_sample)
        return true;
    if (!s_sweep_active || s_trend.first_sample_ms != s_sweep_epoch_first_ms)
    {
        if (!s_sweep_active)
        {
            /* Cold start: slot 0 anchors at the newest sample. */
            s_sweep_epoch_bucket = s_trend.newest_bucket;
            s_sweep_active = true;
        }
        else if (s_trend.newest_bucket >
                 s_sweep_epoch_bucket + 2u * TREND_BUCKET_COUNT)
        {
            /* Buffer reset from a long idle gap (more than one full window
             * behind the epoch): re-anchor so buckets are not all rejected
             * as stale by trend_sweep_bucket_range. */
            s_sweep_epoch_bucket = s_trend.newest_bucket;
        }
        /* A unit-change reset keeps the epoch: re-anchoring it would slide
         * every existing slot sideways at once, which reads as the trace
         * jittering left and right and prevents the sweep from ever
         * reaching its wrap. Just follow the current time. */
        s_sweep_epoch_first_ms = s_trend.first_sample_ms;
        s_sweep_cursor_bucket = s_trend.newest_bucket;
        s_sweep_cycle = (s_trend.newest_bucket - s_sweep_epoch_bucket) /
                        TREND_BUCKET_COUNT;
        s_sweep_scale_lo = s_frame.trend_minimum;
        s_sweep_scale_hi = s_frame.trend_maximum;
        s_sweep_epoch_resets++;
        return true;
    }
    if ((s_trend.newest_bucket - s_sweep_epoch_bucket) / TREND_BUCKET_COUNT >
        s_sweep_cycle)
    {
        /* Wrapped into a new sweep cycle: clear last cycle's trace. */
        s_sweep_cycle =
            (s_trend.newest_bucket - s_sweep_epoch_bucket) /
            TREND_BUCKET_COUNT;
        s_trend_sweep_drawing = true;
        trend_sweep_wipe_cycle();
        s_trend_sweep_drawing = false;
    }
    if (s_frame.trend_minimum != s_sweep_scale_lo ||
        s_frame.trend_maximum != s_sweep_scale_hi)
    {
        /* Axis moved: every drawn slot is at the old scale. Sweep the
         * whole live window again so the trace rescales as one piece —
         * per-slot rescales at different times are what read as jitter. */
        s_sweep_scale_lo = s_frame.trend_minimum;
        s_sweep_scale_hi = s_frame.trend_maximum;
        s_sweep_scale_changes++;
        s_sweep_cursor_bucket =
            target > (TREND_BUCKET_COUNT - 1u)
                ? target - (TREND_BUCKET_COUNT - 1u)
                : s_sweep_epoch_bucket;
    }
    if (target <= s_sweep_cursor_bucket)
        return true;
    steps = target - s_sweep_cursor_bucket;
    if (steps > TREND_BUCKET_COUNT)
    {
        /* The cursor fell more than a full window behind: sweep the whole
         * visible window again. */
        s_sweep_cursor_bucket = target - TREND_BUCKET_COUNT;
        steps = TREND_BUCKET_COUNT;
        budget = TREND_SWEEP_RESCAN_BUDGET;
    }
    else if (target - s_sweep_cursor_bucket > TREND_SWEEP_BUDGET)
    {
        budget = TREND_SWEEP_RESCAN_BUDGET;
    }
    else
    {
        budget = TREND_SWEEP_BUDGET;
    }
    if (steps > budget)
        steps = budget;
    s_trend_sweep_drawing = true;
    for (i = 0u; i < steps; i++)
    {
        uint32_t b = s_sweep_cursor_bucket + 1u + i;
        uint16_t slot = trend_sweep_slot_of_bucket(b);
        uint32_t w0, w1;

        trend_sweep_slot_window(slot, b, &w0, &w1);
        /* Render the slot once, after its last bucket is due (or at the
         * cursor target) so sub-slot samples are merged first. */
        if (b + 1u == w1)
        {
            if (!trend_sweep_render_slot(slot, b))
            {
                s_trend_sweep_drawing = false;
                return false;
            }
        }
    }
    s_trend_sweep_drawing = false;
    s_sweep_cursor_bucket += steps;
    return true;
}

static bool READING_ONLY_LEGACY reading_only_scroll_trend(uint32_t now, uint16_t *scroll_out)
{
    uint16_t scroll;
    uint16_t src_x, src_y, src_w, src_h;
    uint16_t dst_x, dst_y, dst_w, dst_h;
    uint16_t stride = panel_transform_fb_width();
    uint32_t src_addr;
    static uint8_t old_y0[TREND_MAX_COLUMNS];
    static uint8_t old_y1[TREND_MAX_COLUMNS];
    static uint8_t old_occupied[(TREND_MAX_COLUMNS + 7u) / 8u];
    uint16_t col;

    if (s_trend_scroll_ms == 0u)
        return false;
    scroll = (uint16_t)(((uint32_t)(now - s_trend_scroll_ms) *
                         MAIN_DISPLAY_PLOT_W) / TREND_WINDOW_MS);
    if (scroll > MAIN_DISPLAY_PLOT_W / 4u)
        scroll = MAIN_DISPLAY_PLOT_W / 4u;
    if (scroll == 0u)
    {
        *scroll_out = 0u;
        return true;
    }

    src_x = (uint16_t)(MAIN_DISPLAY_PLOT_X + scroll);
    src_y = MAIN_DISPLAY_PLOT_Y;
    src_w = (uint16_t)(MAIN_DISPLAY_PLOT_W - scroll);
    src_h = MAIN_DISPLAY_PLOT_H;
    panel_transform_ui_rect_to_fb(src_x, src_y, src_w, src_h,
                                  &src_x, &src_y, &src_w, &src_h);
    panel_transform_ui_rect_to_fb(MAIN_DISPLAY_PLOT_X, MAIN_DISPLAY_PLOT_Y,
                                  (uint16_t)(MAIN_DISPLAY_PLOT_W - scroll),
                                  MAIN_DISPLAY_PLOT_H,
                                  &dst_x, &dst_y, &dst_w, &dst_h);
    if (src_w == 0u || src_h == 0u || dst_w == 0u || dst_h == 0u)
    {
        s_reading_only_last_error = LT7680_ERR_PARAM;
        s_reading_only_io_error = true;
        return false;
    }
    src_addr = (uint32_t)s_visible_page * 0x00100000u +
               ((uint32_t)src_y * stride + src_x) * 2u;
    if (lt7680_gfx_blit(s_render_page, src_addr, stride, dst_x, dst_y,
                        dst_w, dst_h) != LT7680_OK)
    {
        s_reading_only_last_error = LT7680_ERR_BUS;
        s_reading_only_io_error = true;
        return false;
    }
    if (ui_fill_rect((uint16_t)(MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W - scroll),
                     MAIN_DISPLAY_PLOT_Y, scroll, MAIN_DISPLAY_PLOT_H,
                     MAIN_DISPLAY_COLOR_BG) != LT7680_OK)
        return false;

    /* The blit source is the visible page. Use its raster cache as the source
     * of truth too; the hidden page may intentionally lag after a skipped
     * trend update. Mixing the visible pixels with hidden-page bookkeeping
     * leaves stale segments after the page flip. */
    memcpy(old_y0, s_drawn_trend_y0[s_visible_page], sizeof(old_y0));
    memcpy(old_y1, s_drawn_trend_y1[s_visible_page], sizeof(old_y1));
    memcpy(old_occupied, s_drawn_trend_occupied[s_visible_page], sizeof(old_occupied));
    memset(s_drawn_trend_occupied[s_render_page], 0, sizeof(old_occupied));
    for (col = 0u; col < TREND_MAX_COLUMNS; col++)
    {
        uint16_t x = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                                (uint32_t)col * MAIN_DISPLAY_PLOT_W /
                                TREND_MAX_COLUMNS);
        if (x < MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W - scroll)
        {
            uint16_t source_x = (uint16_t)(x + scroll);
            uint16_t source_col = (uint16_t)(((uint32_t)(source_x - MAIN_DISPLAY_PLOT_X) *
                                              TREND_MAX_COLUMNS) /
                                             MAIN_DISPLAY_PLOT_W);
            if (source_col >= TREND_MAX_COLUMNS)
                source_col = TREND_MAX_COLUMNS - 1u;
            s_drawn_trend_y0[s_render_page][col] = old_y0[source_col];
            s_drawn_trend_y1[s_render_page][col] = old_y1[source_col];
            if ((old_occupied[source_col >> 3] & (uint8_t)(1u << (source_col & 7u))) != 0u)
                s_drawn_trend_occupied[s_render_page][col >> 3] |=
                    (uint8_t)(1u << (col & 7u));
        }
    }
    *scroll_out = scroll;
    return true;
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

/* ADR-0006: baseline draws no axis labels; kept for the legacy renderer. */
static uint16_t READING_ONLY_LEGACY trend_x_label_x(uint16_t center, const char *label)
{
    size_t width = strlen(label) * FONT_TEXT_WIDTH;
    if (center == MAIN_DISPLAY_PLOT_X && width <= MAIN_DISPLAY_PLOT_W)
        return MAIN_DISPLAY_PLOT_X;
    uint16_t x = center > width / 2u ? (uint16_t)(center - width / 2u)
                                     : MAIN_DISPLAY_PLOT_X;
    if (x + width > MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W)
        x = (uint16_t)(MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W - width);
    return x;
}


/* True when any rendered status/info text differs from what is on screen.
 * Status TAGs arrive with every sample, but a top-bar + info-cell repaint
 * costs thousands of GE fill transactions; only a real content change may
 * schedule one. The perf GPIB string is deliberately excluded (it changes
 * every frame and is decorative). */
static bool status_view_changed(void)
{
    static const uint8_t lamp_idx[8] = {0u, 1u, 2u, 3u, 5u, 7u, 6u, 11u};
    static char prev_impedance[12];
    static char prev_range[12];
    static char prev_rate[16];
    static uint8_t prev_lamps;
    static uint8_t prev_info_lamps;
    static bool valid;
    uint8_t top_lamps = 0u;
    uint8_t info_lamps = 0u;
    uint8_t dirty_rows = 0u;
    bool top_changed;
    uint8_t i;

    for (i = 0u; i < 6u; i++)
        if (s_frame.status_active[lamp_idx[i]])
            top_lamps = (uint8_t)(top_lamps | (uint8_t)(1u << i));
    if (s_frame.status_active[7u])
        info_lamps |= 1u;
    if (s_frame.status_active[6u])
        info_lamps |= 2u;
    if (s_frame.status_active[11u])
        info_lamps |= 4u;
    top_changed = !valid || top_lamps != prev_lamps;
    if (!valid || strcmp(prev_impedance, s_frame.impedance) != 0)
        dirty_rows |= 1u << 0;
    if (!valid || strcmp(prev_range, s_frame.range) != 0)
        dirty_rows |= 1u << 1;
    if (!valid || strcmp(prev_rate, s_frame.rate) != 0)
        dirty_rows |= 1u << 2;
    if (!valid || info_lamps != prev_info_lamps)
        dirty_rows |= 1u << 3;
    strncpy(prev_impedance, s_frame.impedance, sizeof(prev_impedance) - 1u);
    prev_impedance[sizeof(prev_impedance) - 1u] = '\0';
    strncpy(prev_range, s_frame.range, sizeof(prev_range) - 1u);
    prev_range[sizeof(prev_range) - 1u] = '\0';
    strncpy(prev_rate, s_frame.rate, sizeof(prev_rate) - 1u);
    prev_rate[sizeof(prev_rate) - 1u] = '\0';
    prev_lamps = top_lamps;
    prev_info_lamps = info_lamps;
    valid = true;
    s_status_info_dirty_rows = dirty_rows;
    return top_changed || dirty_rows != 0u;
}

/* Re-derive every TEXT field of the shared snapshot from the live ui model
 * while keeping the trend composition state of the running frame intact:
 * an interrupted graph pass keeps consuming y_labels/bounds across the
 * preempted region phases. x_labels are constants that format refills
 * identically; y_labels are regenerated from the saved resident identity. */
static void reading_refresh_text_snapshot(void)
{
    bool trend_has_data = s_frame.trend_has_data;
    float trend_minimum = s_frame.trend_minimum;
    float trend_maximum = s_frame.trend_maximum;
    float axis_step = s_frame.trend_axis_step;
    float axis_top = s_frame.trend_axis_top;
    char axis_unit[sizeof(s_frame.trend_axis_unit)];

    memcpy(axis_unit, s_frame.trend_axis_unit, sizeof(axis_unit));
    main_display_format(&s_ui, &s_frame);
    s_frame.trend_has_data = trend_has_data;
    s_frame.trend_minimum = trend_minimum;
    s_frame.trend_maximum = trend_maximum;
    s_frame.trend_axis_step = axis_step;
    s_frame.trend_axis_top = axis_top;
    memcpy(s_frame.trend_axis_unit, axis_unit, sizeof(axis_unit));
    if (trend_has_data && axis_step > 0.0f)
        main_display_set_trend_axis(&s_frame, axis_step, axis_top,
                                    axis_unit);
}

/* Review I-1: region dirties arriving DURING an active trend pass used to
 * wait in s_ui_dirty_regions until the whole pass finished -- the
 * slice-boundary yield never saw scheduler work, so readings waited out
 * long graph rebuilds. Wire the bits into the scheduler here instead:
 * request_regions only raises pending bits while a phase is active (the
 * running bitmap job is never restarted), and the next trend slice
 * boundary hands control to the higher-priority region phase. The shared
 * text snapshot is refreshed first so the preempted STATUS/READING phases
 * draw the newest accepted values, with the same content gate as the
 * idle-path snapshot (status TAGs ride every sample). */
static void READING_ONLY_LEGACY trend_wire_pending_regions(void)
{
    uint8_t due;
    bool status_due;

    if ((s_ui_dirty_regions &
         (RENDER_DIRTY_STATUS | RENDER_DIRTY_READING)) == 0u)
    {
        return;
    }
    reading_refresh_text_snapshot();
    /* Reading renders share ONE throttle clock with the idle path, so the
     * combined rate never exceeds DISPLAY_FRAME_PERIOD_MS regardless of
     * where they run. Mid-pass service matters: an axis rescale remaps all
     * 240 columns as one unpreemptible-ish burst, and without this the
     * digits freeze for its full duration (user-visible stall). */
    due = (uint8_t)(s_ui_dirty_regions & RENDER_DIRTY_READING);
    if ((due & RENDER_DIRTY_READING) != 0u &&
        (HAL_GetTick() - s_text_refresh_tick) < DISPLAY_FRAME_PERIOD_MS)
    {
        due &= (uint8_t)~RENDER_DIRTY_READING;
    }
    status_due = (s_ui_dirty_regions & RENDER_DIRTY_STATUS) != 0u;
    s_ui_dirty_regions &= (uint8_t)~RENDER_DIRTY_STATUS;
    if ((due & RENDER_DIRTY_READING) != 0u)
        s_ui_dirty_regions &= (uint8_t)~RENDER_DIRTY_READING;
    if (status_due)
    {
        if (status_view_changed())
        {
            due |= RENDER_DIRTY_STATUS;
            /* A later reading in this composition must repaint the info
             * cells alongside the changed status rows. */
            s_render_status_regions = true;
        }
    }

    if ((due & RENDER_DIRTY_READING) != 0u)
    {
        s_text_generation++;
        s_perf_reading_frames_window++;
        s_frame_text_generation = s_text_generation;
        s_text_refresh_tick = HAL_GetTick();
    }
    if (due == 0u)
    {
        return;
    }
    render_scheduler_request_regions(&s_renderer, due);
}

/* Bounded-slice handoff to higher-priority region work (review I-2): on a
 * successful yield the interrupted trend phase's item cursor is dropped --
 * STATUS/READING initialize their own item sequences at 0, and an
 * interrupted AXES pass replays deterministically from item 0 (safe: no
 * column of that pass exists yet); COLUMNS carries progress in
 * s_render_column, which the region phases never touch. */
static bool READING_ONLY_LEGACY trend_yield_to_regions(void)
{
    if (!render_scheduler_yield_trend(&s_renderer))
    {
        return false;
    }
    s_render_item = 0u;
    return true;
}

#if K2000_READING_ONLY_BASELINE
/* Row-1 left info (ADR-0004): all active core lamps EXCEPT the ones duplicated
 * in row 2 (REL=6, FILT=7, AUTO=8), space-joined. TRIG stays (steady lamp);
 * row 2 owns the event-style TRIGGER + blinking dot. */
static void row1_status_text(char *out, uint8_t size)
{
    static const uint8_t order[] = {0u, 1u, 2u, 3u, 4u, 5u, 9u, 10u, 11u, 12u};
    static const char *const labels[] = {"REMOTE", "TALK", "LSTN", "SRQ", "HOLD",
                                         "TRIG", "ERR", "BUFFER", "MATH", "CONT"};
    uint8_t i;
    bool first = true;

    if (out == 0 || size == 0u) return;
    out[0] = '\0';
    for (i = 0u; i < (uint8_t)(sizeof(order) / sizeof(order[0])); i++)
    {
        uint8_t k = order[i];
        size_t n;
        if (k >= STATUS_BAR_CORE_COUNT || !s_frame.status_active[k]) continue;
        n = strlen(out);
        if (!first && n + 1u < size) { out[n] = ' '; out[n + 1u] = '\0'; n++; }
        {
            const char *s = labels[i];
            while (*s != '\0' && n + 1u < size) out[n++] = *s++;
            out[n] = '\0';
        }
        first = false;
    }
}

/* Brand split (ADR-0004): only "KEITHLEY" rides the red badge (black text);
 * the "2000" tail stays plain white on BAR. Brand is "AAA...A TAIL", split at
 * the first space; no space = whole brand on red (fallback). */
static size_t row1_brand_head_len(const char *brand)
{
    const char *sp = brand != 0 ? strchr(brand, ' ') : 0;
    return sp != 0 ? (size_t)(sp - brand) : strlen(brand);
}

static const char *row1_brand_tail(const char *brand)
{
    const char *sp = brand != 0 ? strchr(brand, ' ') : 0;
    if (sp == 0) return "";
    while (*sp == ' ') sp++;
    return sp;
}

/* Red rect end for a brand string (text at x12 + head + right pad). */
static uint16_t row1_red_end_for(const char *brand)
{
    return (uint16_t)(12u + row1_brand_head_len(brand) * FONT_TEXT_WIDTH +
                      MAIN_DISPLAY_BADGE_PAD_X);
}

/* Full brand text end (red end + gap + tail), anchors sep + info lamps. */
static uint16_t row1_text_end_for(const char *brand)
{
    return (uint16_t)(row1_red_end_for(brand) + MAIN_DISPLAY_BRAND_TAIL_GAP +
                      strlen(row1_brand_tail(brand)) * FONT_TEXT_WIDTH);
}

static uint16_t row1_brand_end_x(void)
{
    return row1_text_end_for(s_frame.brand);
}

static uint16_t row1_info_x(void)
{
    return (uint16_t)(row1_brand_end_x() + MAIN_DISPLAY_ROW1_SEP_GAP +
                      MAIN_DISPLAY_ROW1_SEP_W + MAIN_DISPLAY_ROW1_INFO_GAP);
}

/* Brand-row extras (NOT status lamps): white 0x07 SHIFT/REAR right after the
 * logo. One string, one step (same resumable-job rule as the lamp string).
 * An active SHIFT turns the whole row-1 background blue. */
static uint16_t row1_bg(void)
{
    return s_frame.shift_active ? MAIN_DISPLAY_COLOR_BLUE
                                : MAIN_DISPLAY_COLOR_BAR;
}

static void row1_extra_text(char *out, uint8_t size)
{
    size_t n;
    if (out == 0 || size == 0u) return;
    out[0] = '\0';
    if (s_frame.shift_active) {
        const char *s = "SHIFT";
        n = 0u;
        while (s[n] != '\0' && n + 1u < size) { out[n] = s[n]; n++; }
        out[n] = '\0';
    }
    if (s_frame.rear_active) {
        const char *s = "REAR";
        n = strlen(out);
        if (n > 0u && n + 1u < size) { out[n] = ' '; out[n + 1u] = '\0'; n++; }
        {
            uint8_t k = 0u;
            while (s[k] != '\0' && n + 1u < size) { out[n++] = s[k++]; }
            out[n] = '\0';
        }
    }
}

static uint16_t row1_extra_w(const char *extra)
{
    return (uint16_t)(strlen(extra != 0 ? extra : "") * FONT_TEXT_WIDTH);
}

/* Green lamp string origin: brand end + sep + white extras + gap. With no
 * extras this is exactly the old row1_info_x() (pixel-identical). */
static uint16_t row1_lamp_x_for(const char *brand, const char *extra)
{
    uint16_t x = (uint16_t)(row1_text_end_for(brand) +
                            MAIN_DISPLAY_ROW1_SEP_GAP +
                            MAIN_DISPLAY_ROW1_SEP_W +
                            MAIN_DISPLAY_ROW1_INFO_GAP +
                            row1_extra_w(extra));
    if (extra != 0 && extra[0] != '\0') x += MAIN_DISPLAY_ROW1_INFO_GAP;
    return x;
}

static bool trend_paint_cell_diff(const char *old_text, const char *new_text,
                                  uint16_t x, uint16_t y, uint16_t h, uint16_t color,
                                  char *saved, uint8_t saved_size,
                                  uint8_t *pos, uint8_t budget);

static bool reading_only_render_status_bar(void)
{
    static const uint8_t status_bits[5] = {0u, 1u, 2u, 3u, 5u};
    static uint8_t idx;
    static uint8_t uptime_diff_pos;
    bool brand_dirty, active_dirty, temp_dirty, uptime_dirty;
    bool shift_changed;
    char cur_extra[16];
    if (s_reading_only_stage != READING_ONLY_STATUS) idx = 0u;
    row1_extra_text(cur_extra, sizeof(cur_extra));
    /* A SHIFT/REAR change repaints the brand row: extras text, lamp origin
     * and (for SHIFT) the whole row background. */
    shift_changed = !s_reading_only_page_status_valid[s_render_page] ||
                    s_reading_only_page_shift[s_render_page] != s_frame.shift_active ||
                    s_reading_only_page_rear[s_render_page] != s_frame.rear_active ||
                    strcmp(s_reading_only_page_extra[s_render_page], cur_extra) != 0;
    brand_dirty = shift_changed ||
                  strcmp(s_reading_only_page_brand[s_render_page], s_frame.brand) != 0;
    {
        char cur_row1[MAIN_DISPLAY_META_MAX];
        row1_status_text(cur_row1, sizeof(cur_row1));
        active_dirty = strcmp(s_reading_only_page_row1[s_render_page], cur_row1) != 0;
    }
    temp_dirty = shift_changed ||
                 strcmp(s_reading_only_page_temperature[s_render_page], s_frame.temperature) != 0;
    uptime_dirty = shift_changed ||
                   strcmp(s_reading_only_page_uptime[s_render_page], s_frame.uptime) != 0;
    // Use layout framework to compute positions and detect overlap
    {
        uint16_t bx, ax, tx, ux;
        bool ok = ui_layout_top_bar(s_frame.brand, s_frame.active_status, s_frame.temperature, s_frame.uptime, &bx, &ax, &tx, &ux);
        if(!ok){
            // overlap detected, prefer right-aligned temp/uptime, squeeze active
        }
        (void)ok;
    }
    if (idx == 0u) {
        if (!s_reading_only_page_status_valid[s_render_page]) {
            if (ui_fill_rect(0u, 0u, MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_STATUS_H, row1_bg()) != LT7680_OK) return false;
            idx++;
            return false;
        } else { idx++; }
    }
    if (idx == 1u) {
        /* Red KEITHLEY badge (ADR-0004): solid red rect under the head only;
         * the "2000" tail is drawn plain two steps later. With SHIFT active
         * the whole row is blue instead (no red badge). */
        if (!brand_dirty) { idx = 6u; } else {
            uint16_t w = row1_red_end_for(s_frame.brand);
            uint16_t fw = row1_text_end_for(s_frame.brand);
            uint16_t ofw = row1_text_end_for(s_reading_only_page_brand[s_render_page]);
            uint16_t cw = fw > ofw ? fw : ofw;
            if (s_frame.shift_active != s_reading_only_page_shift[s_render_page] ||
                !s_reading_only_page_status_valid[s_render_page]) {
                /* Background transition (or first paint): stale pixels can
                 * sit anywhere on the row, so repaint the full width. */
                if (ui_fill_rect(0u, 0u, MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_STATUS_H, row1_bg()) != LT7680_OK) return false;
            } else {
                /* Clear covers badge + tail + separator + gap so a narrower
                 * brand cannot leave stale pixels behind. */
                cw = (uint16_t)(cw + MAIN_DISPLAY_ROW1_SEP_GAP +
                                MAIN_DISPLAY_ROW1_SEP_W + MAIN_DISPLAY_ROW1_INFO_GAP);
                if (ui_fill_rect(0u, 0u, cw, MAIN_DISPLAY_STATUS_H, row1_bg()) != LT7680_OK) return false;
            }
            if (!s_frame.shift_active) {
                if (ui_fill_rect(0u, 0u, w, MAIN_DISPLAY_STATUS_H, MAIN_DISPLAY_COLOR_BRAND_BG) != LT7680_OK) return false;
            }
            idx++;
            return false;
        }
    }
    if (idx == 2u) {
        /* 黑屏教训：一步内连续两次 ui_draw_text 会饿死——作业完成后的再次
         * 调用被当成新任务从头重绘，idx 永不推进、帧永不提交。头/尾必须各
         * 占一步（ADR-0004§Bug2）。 */
        if (!brand_dirty) { idx++; } else {
            char head[MAIN_DISPLAY_META_MAX];
            size_t n = row1_brand_head_len(s_frame.brand);
            if (n >= sizeof(head)) n = sizeof(head) - 1u;
            memcpy(head, s_frame.brand, n);
            head[n] = '\0';
            if (!ui_draw_text(12u, 0u, head, MAIN_DISPLAY_COLOR_BRAND_TEXT)) return false;
            idx++;
            return false;
        }
    }
    if (idx == 3u) {
        if (!brand_dirty) { idx++; } else {
            uint16_t tail_x = (uint16_t)(row1_red_end_for(s_frame.brand) +
                                         MAIN_DISPLAY_BRAND_TAIL_GAP);
            if (!ui_draw_text(tail_x, 0u, row1_brand_tail(s_frame.brand),
                              MAIN_DISPLAY_COLOR_WHITE)) return false;
            idx++;
            return false;
        }
    }
    if (idx == 4u) {
        if (!brand_dirty) { idx++; } else {
            if (ui_fill_rect((uint16_t)(row1_brand_end_x() + MAIN_DISPLAY_ROW1_SEP_GAP),
                             (uint16_t)((MAIN_DISPLAY_STATUS_H - MAIN_DISPLAY_ROW1_SEP_H) / 2u),
                             MAIN_DISPLAY_ROW1_SEP_W, MAIN_DISPLAY_ROW1_SEP_H,
                             MAIN_DISPLAY_COLOR_SEP) != LT7680_OK) return false;
            idx++;
            return false;
        }
    }
    if (idx == 5u) {
        /* White SHIFT/REAR right after the logo (own step: one string per
         * resumable bitmap job, same rule as head/tail/lamps). */
        char cur[16];
        uint16_t nx = row1_info_x();
        uint16_t nw, ow, fx, xe;
        uint16_t ox = nx;
        row1_extra_text(cur, sizeof(cur));
        ow = row1_extra_w(s_reading_only_page_extra[s_render_page]);
        /* Old origin moves with brand width; same first-paint guard as the
         * lamp string below (empty snapshot => old origin meaningless). */
        if (ow > 0u)
        {
            uint16_t oo = (uint16_t)(row1_text_end_for(s_reading_only_page_brand[s_render_page]) +
                                     MAIN_DISPLAY_ROW1_SEP_GAP +
                                     MAIN_DISPLAY_ROW1_SEP_W + MAIN_DISPLAY_ROW1_INFO_GAP);
            if (oo < ox) ox = oo;
        }
        nw = row1_extra_w(cur);
        fx = ox < nx ? ox : nx;
        xe = ox + ow > nx + nw ? ox + ow : nx + nw;
        if (!brand_dirty) { idx++; } else {
            if (!s_bitmap_job.active) {
                if (xe > fx &&
                    ui_fill_rect(fx, 0u, (uint16_t)(xe - fx), MAIN_DISPLAY_STATUS_H,
                                 row1_bg()) != LT7680_OK) return false;
            }
            if (cur[0] != '\0' && !ui_draw_text(nx, 0u, cur, MAIN_DISPLAY_COLOR_WHITE)) return false;
            idx++;
            return false;
        }
    }
    if (idx == 6u) {
        /* Left-aligned de-duplicated lamps (ADR-0004): single string keeps the
         * resumable bitmap job safe (one job, one string per step). Origin
         * follows the white extras (pixel-identical to before when empty). */
        char cur[MAIN_DISPLAY_META_MAX];
        char extra[16];
        uint16_t nx;
        uint16_t nw, ow, fx, xe;
        uint16_t ox;
        row1_extra_text(extra, sizeof(extra));
        nx = row1_lamp_x_for(s_frame.brand, extra);
        ox = nx;
        row1_status_text(cur, sizeof(cur));
        ow = (uint16_t)(strlen(s_reading_only_page_row1[s_render_page]) * FONT_TEXT_WIDTH);
        /* Old origin moves with brand width AND old extras width; erase the
         * union of both extents. 首刷边界(ADR-0004§Bug)：缓存为空时旧原点无意义（按空品牌算出 x33），
         * 并集擦除会抹掉刚画好的品牌只剩 "KE"——此时只擦新范围。 */
        if (ow > 0u)
        {
            uint16_t oo = row1_lamp_x_for(s_reading_only_page_brand[s_render_page],
                                          s_reading_only_page_extra[s_render_page]);
            if (oo < ox) ox = oo;
        }
        nw = (uint16_t)(strlen(cur) * FONT_TEXT_WIDTH);
        fx = ox < nx ? ox : nx;
        xe = ox + ow > nx + nw ? ox + ow : nx + nw;
        if (!active_dirty && !brand_dirty && strcmp(s_reading_only_page_row1[s_render_page], cur) == 0) { idx++; } else {
            if (!s_bitmap_job.active) {
                if (xe > fx &&
                    ui_fill_rect(fx, 0u, (uint16_t)(xe - fx), MAIN_DISPLAY_STATUS_H,
                                 row1_bg()) != LT7680_OK) return false;
            }
            if (cur[0] != '\0' && !ui_draw_text(nx, 0u, cur, MAIN_DISPLAY_COLOR_GREEN)) return false;
            idx++;
            return false;
        }
    }
    if (idx == 7u) {
        if (!temp_dirty) { idx++; } else {
            if (!s_bitmap_job.active) {
                uint16_t n_right_w = (uint16_t)((strlen(s_frame.temperature) + 2u + strlen(s_frame.uptime)) * FONT_TEXT_WIDTH);
                uint16_t n_right_x = (uint16_t)(MAIN_DISPLAY_UI_WIDTH - n_right_w - 12u);
                uint16_t nw = (uint16_t)(strlen(s_frame.temperature) * FONT_TEXT_WIDTH);
                uint16_t ow = (uint16_t)(strlen(s_reading_only_page_temperature[s_render_page]) * FONT_TEXT_WIDTH);
                uint16_t fw = nw > ow ? nw : ow;
                if (ui_fill_rect(n_right_x, 0u, fw, MAIN_DISPLAY_STATUS_H, row1_bg()) != LT7680_OK) return false;
            }
            uint16_t n_right_w2 = (uint16_t)((strlen(s_frame.temperature) + 2u + strlen(s_frame.uptime)) * FONT_TEXT_WIDTH);
            uint16_t n_right_x2 = (uint16_t)(MAIN_DISPLAY_UI_WIDTH - n_right_w2 - 12u);
            if (!ui_draw_text(n_right_x2, 0u, s_frame.temperature, MAIN_DISPLAY_COLOR_WHITE)) return false;
            idx++;
            return false;
        }
    }
    if (idx == 8u) {
        /* Right-side half-height separator between temperature and uptime
         * (1x12 grid, same language as the brand separator). The column
         * tracks the right-aligned block; erase the union of the old and
         * new columns so a width change leaves no stale pixel. */
        if (s_reading_only_page_status_valid[s_render_page] &&
            !temp_dirty && !uptime_dirty) { idx++; } else {
            uint16_t n_tw = (uint16_t)(strlen(s_frame.temperature) * FONT_TEXT_WIDTH);
            uint16_t o_tw = (uint16_t)(strlen(s_reading_only_page_temperature[s_render_page]) * FONT_TEXT_WIDTH);
            uint16_t n_rw = (uint16_t)((strlen(s_frame.temperature) + 2u + strlen(s_frame.uptime)) * FONT_TEXT_WIDTH);
            uint16_t o_rw = (uint16_t)((strlen(s_reading_only_page_temperature[s_render_page]) + 2u + strlen(s_reading_only_page_uptime[s_render_page])) * FONT_TEXT_WIDTH);
            uint16_t n_sx = (uint16_t)(MAIN_DISPLAY_UI_WIDTH - n_rw - 12u + n_tw + 11u);
            uint16_t o_sx = (uint16_t)(MAIN_DISPLAY_UI_WIDTH - o_rw - 12u + o_tw + 11u);
            uint16_t fx = n_sx < o_sx ? n_sx : o_sx;
            uint16_t xe = (uint16_t)((n_sx > o_sx ? n_sx : o_sx) + 1u);
            if (!s_reading_only_page_status_valid[s_render_page]) { fx = n_sx; xe = (uint16_t)(n_sx + 1u); }
            if (ui_fill_rect(fx, 0u, (uint16_t)(xe - fx), MAIN_DISPLAY_STATUS_H, row1_bg()) != LT7680_OK) return false;
            if (ui_fill_rect(n_sx, (uint16_t)((MAIN_DISPLAY_STATUS_H - MAIN_DISPLAY_ROW1_SEP_H) / 2u),
                             MAIN_DISPLAY_ROW1_SEP_W, MAIN_DISPLAY_ROW1_SEP_H,
                             MAIN_DISPLAY_COLOR_SEP) != LT7680_OK) return false;
            idx++;
            return false;
        }
    }
    if (idx == 9u) {
        if (!uptime_dirty) { idx++; uptime_diff_pos = 0u; } else {
            uint16_t n_right_w = (uint16_t)((strlen(s_frame.temperature) + 2u + strlen(s_frame.uptime)) * FONT_TEXT_WIDTH);
            uint16_t n_right_x = (uint16_t)(MAIN_DISPLAY_UI_WIDTH - n_right_w - 12u);
            uint16_t ux = (uint16_t)(n_right_x + strlen(s_frame.temperature) * FONT_TEXT_WIDTH + 24u);
            /* Steady-state uptime (fixed 8 glyphs, temp unchanged): repaint
             * only the changed digit cells instead of erase-all + full
             * redraw (~60 fills ≈ 20 ms every second). The cell painter
             * assumes BAR background, so the blue SHIFT row always takes
             * the full erase + redraw path below. */
            if (!temp_dirty && !s_frame.shift_active &&
                strlen(s_reading_only_page_uptime[s_render_page]) == strlen(s_frame.uptime)) {
                if (!trend_paint_cell_diff(s_reading_only_page_uptime[s_render_page],
                                           s_frame.uptime, ux, 0u,
                                           MAIN_DISPLAY_STATUS_H, MAIN_DISPLAY_COLOR_WHITE,
                                           s_reading_only_page_uptime[s_render_page],
                                           sizeof(s_reading_only_page_uptime[0]),
                                           &uptime_diff_pos, 6u)) {
                    if (s_reading_only_io_error) { idx = 0u; uptime_diff_pos = 0u; }
                    return false;
                }
                uptime_diff_pos = 0u;
                idx++;
                return false;
            }
            uptime_diff_pos = 0u;
            if (!s_bitmap_job.active) {
                uint16_t nw = (uint16_t)(strlen(s_frame.uptime) * FONT_TEXT_WIDTH);
                uint16_t ow = (uint16_t)(strlen(s_reading_only_page_uptime[s_render_page]) * FONT_TEXT_WIDTH);
                uint16_t fw = nw > ow ? nw : ow;
                if (ui_fill_rect(ux, 0u, fw, MAIN_DISPLAY_STATUS_H, row1_bg()) != LT7680_OK) return false;
            }
            uint16_t n_right_w3 = (uint16_t)((strlen(s_frame.temperature) + 2u + strlen(s_frame.uptime)) * FONT_TEXT_WIDTH);
            uint16_t n_right_x3 = (uint16_t)(MAIN_DISPLAY_UI_WIDTH - n_right_w3 - 12u);
            uint16_t ux2 = (uint16_t)(n_right_x3 + strlen(s_frame.temperature) * FONT_TEXT_WIDTH + 24u);
            if (!ui_draw_text(ux2, 0u, s_frame.uptime, MAIN_DISPLAY_COLOR_WHITE)) return false;
            idx++;
            return false;
        }
    }
    if (idx == 10u) { idx++; }
    idx = 0u;
    s_reading_only_page_status_valid[s_render_page] = true;
    row1_extra_text(s_reading_only_page_extra[s_render_page],
                    sizeof(s_reading_only_page_extra[0]));
    s_reading_only_page_shift[s_render_page] = s_frame.shift_active;
    s_reading_only_page_rear[s_render_page] = s_frame.rear_active;
    strncpy(s_reading_only_page_brand[s_render_page], s_frame.brand,
            sizeof(s_reading_only_page_brand[0]) - 1u);
    s_reading_only_page_brand[s_render_page][sizeof(s_reading_only_page_brand[0]) - 1u] = '\0';
    strncpy(s_reading_only_page_active_status[s_render_page], s_frame.active_status,
            sizeof(s_reading_only_page_active_status[0]) - 1u);
    s_reading_only_page_active_status[s_render_page]
        [sizeof(s_reading_only_page_active_status[0]) - 1u] = '\0';
    strncpy(s_reading_only_page_temperature[s_render_page], s_frame.temperature,
            sizeof(s_reading_only_page_temperature[0]) - 1u);
    strncpy(s_reading_only_page_uptime[s_render_page], s_frame.uptime,
            sizeof(s_reading_only_page_uptime[0]) - 1u);
    row1_status_text(s_reading_only_page_row1[s_render_page],
                     sizeof(s_reading_only_page_row1[0]));
    {
        uint8_t cur = 0u;
    for (uint8_t i = 0u; i < 5u; i++) if (s_frame.status_active[status_bits[i]]) cur |= (1u<<i);
        s_reading_only_page_status_lamps[s_render_page] = cur;
    }
    return true;
}

/* Row-2 geometry (ADR-0004): green badge, stats-style cells, right trigger. */
static uint16_t row2_text_y(void)
{
    return (uint16_t)(MAIN_DISPLAY_INFO_BAR_Y +
                      (MAIN_DISPLAY_INFO_BAR_H - FONT_TEXT_HEIGHT) / 2u);
}

#if K2000_ROW_DUMP
static void row_debug_dump(void)
{
    uint16_t ys[3];
    uint8_t r;
    ys[0] = 12u;
    ys[1] = (uint16_t)(row2_text_y() + 6u);
    ys[2] = (uint16_t)(row2_text_y() + 18u);
    (void)lt7680_gfx_select_canvas_page(s_visible_page);
    (void)lt7680_gfx_set_canvas_width(320u);
    for (r = 0u; r < 3u; r++)
    {
        uint16_t x;
        hal_uart_send_text("\r\nROWDUMP y=");
        perf_send_u32(ys[r]);
        hal_uart_send_text(" ");
        for (x = 0u; x < MAIN_DISPLAY_UI_WIDTH; x += 4u)
        {
            uint16_t fx, fy, px = 0xFFFFu;
            panel_transform_ui_to_fb(x, ys[r], &fx, &fy);
            (void)lt7680_gfx_peek_pixel(fx, fy, &px);
            hal_uart_send_hex8((uint8_t)(px >> 8));
            hal_uart_send_hex8((uint8_t)px);
        }
        hal_uart_send_text("\r\n");
    }
    (void)lt7680_gfx_select_canvas_page(s_render_page);
    (void)lt7680_gfx_set_canvas_width(320u);
}
#endif

static uint16_t row2_trig_dot_x(void)
{
    return (uint16_t)(MAIN_DISPLAY_UI_WIDTH - MAIN_DISPLAY_TRIG_MARGIN_R -
                      MAIN_DISPLAY_TRIG_DOT_SIZE);
}

static uint16_t row2_trig_dot_y(void)
{
    return (uint16_t)(MAIN_DISPLAY_INFO_BAR_Y +
                      (MAIN_DISPLAY_INFO_BAR_H - MAIN_DISPLAY_TRIG_DOT_SIZE) / 2u);
}

static uint16_t row2_trig_text_x(void)
{
    return (uint16_t)(row2_trig_dot_x() - MAIN_DISPLAY_ROW2_TRIG_GAP -
                      7u * FONT_TEXT_WIDTH);
}

static uint16_t row2_trig_sep_x(void)
{
    return (uint16_t)(row2_trig_text_x() - MAIN_DISPLAY_ROW2_TRIG_GAP);
}

static uint8_t row2_info_lamps(void)
{
    /* FILT/REL/MATH + TRIG bit: any change (incl. trigger on/off) forces a
     * full row repaint so stale pixels can never survive. */
    return (uint8_t)((s_frame.status_active[7u] ? 1u : 0u) |
                     (s_frame.status_active[6u] ? 2u : 0u) |
                     (s_frame.status_active[11u] ? 4u : 0u) |
                     (s_frame.status_active[5u] ? 8u : 0u));
}

/* Row-2 fixed cells (ADR-0007): positions never move — only text changes.
 * The old flow layout (bx cursor from measured widths) pushed every cell
 * after a widened value (Range!) sideways on every repaint: visible squeeze
 * flicker. Badge keeps its measured width (function changes are rare and
 * atomic); cells start at fixed origins past the widest badge. */
#define ROW2_X_ZIN 162u
#define ROW2_W_ZIN 108u
#define ROW2_X_RANGE 280u
#define ROW2_W_RANGE 248u
#define ROW2_X_RATE 538u
#define ROW2_W_RATE 120u
#define ROW2_X_SHORT_SEP 658u
static const uint16_t row2_lamp_x[3] = {666u, 726u, 774u};

static bool reading_only_render_info_panel(void)
{
    static uint8_t idx;
    static uint16_t badge_w;
    /* Right statuses: active-only (ADR-0004), same rule as row 1. */
    static const char *const lamps[3] = {"FILT", "REL", "MATH"};
    static const uint8_t lamp_bits[3] = {7u, 6u, 11u};
    static const char *const cell_names[3] = {"Zin", "Range", "Rate"};
    static const uint16_t cell_x[3] = {ROW2_X_ZIN, ROW2_X_RANGE, ROW2_X_RATE};
    static const uint16_t cell_w[3] = {ROW2_W_ZIN, ROW2_W_RANGE, ROW2_W_RATE};
    bool page_valid = s_reading_only_page_info_valid[s_render_page];
    bool function_dirty = !page_valid ||
                          strcmp(s_reading_only_page_function[s_render_page],
                                 s_frame.function) != 0;
    bool cell_dirty[3] = {
        !page_valid || strcmp(s_reading_only_page_impedance[s_render_page],
                              s_frame.impedance) != 0,
        !page_valid || strcmp(s_reading_only_page_range[s_render_page],
                              s_frame.range) != 0,
        !page_valid || strcmp(s_reading_only_page_rate[s_render_page],
                              s_frame.rate) != 0};
    bool lamps_dirty = !page_valid ||
                       s_reading_only_page_info_lamps[s_render_page] !=
                           row2_info_lamps();
    /* Values are strictly left-anchored at a fixed origin (left-aligned):
     * same-start overwrite is exact, so only a longer old tail or text
     * past the zone can ghost. A value longer than its zone spills into
     * the neighbour cell — force that neighbour to repaint as well,
     * otherwise spilled pixels sit on static text forever. */
    bool lamps_overflow = false;
    {
        const char *old_vals[3] = {
            s_reading_only_page_impedance[s_render_page],
            s_reading_only_page_range[s_render_page],
            s_reading_only_page_rate[s_render_page]};
        const char *new_vals[3] = {s_frame.impedance, s_frame.range,
                                   s_frame.rate};
        uint8_t k;
        for (k = 0u; k < 3u; k++)
        {
            uint16_t zw = (uint16_t)(cell_w[k] -
                (strlen(cell_names[k]) * FONT_TEXT_WIDTH +
                 2u * MAIN_DISPLAY_ROW2_CELL_PAD_X));
            uint16_t ow = page_valid ? ui_measure_text(old_vals[k]) : 0u;
            uint16_t nw = ui_measure_text(new_vals[k]);
            if (ow > zw || nw > zw)
            {
                if (k < 2u)
                    cell_dirty[k + 1u] = true;
                else
                    lamps_dirty = true;
                if (k == 2u)
                    lamps_overflow = true;
            }
        }
    }
    if (s_reading_only_stage != READING_ONLY_INFO) idx = 0u;
    {
        uint16_t fw, zx, ix, rlx, rx, atlx, atx, lx;
        bool fit = ui_layout_second_row(s_frame.function, s_frame.impedance, s_frame.range, s_frame.rate, &fw, &zx, &ix, &rlx, &rx, &atlx, &atx, &lx);
        if(!fit){
            // squeezed, values may be truncated - still render with pushed positions
        }
        (void)fw; (void)zx; (void)ix; (void)rlx; (void)rx; (void)atlx; (void)atx; (void)lx;
        // overlap detection via ui_rect_overlap for function vs Zin
        ui_rect_t fr = ui_rect_from_text(12u, MAIN_DISPLAY_INFO_BAR_Y, s_frame.function, MAIN_DISPLAY_INFO_BAR_H);
        ui_rect_t zr = {240u, MAIN_DISPLAY_INFO_BAR_Y, 3u*FONT_TEXT_WIDTH, MAIN_DISPLAY_INFO_BAR_H};
        if(ui_rect_overlap(&fr,&zr)){
            // push would be needed, logged for framework verification
        }
    }
    /* Green badge档位标识 (ADR-0004: badge + divider both green) + InfoBar下沿
     * 单条绿色分隔线。边界约束(ADR-0003): INFO阶段只允许触碰 y24..51。 */
    if (s_reading_only_page_info_valid[s_render_page] &&
        strcmp(s_reading_only_page_function[s_render_page], s_frame.function) == 0 &&
        strcmp(s_reading_only_page_impedance[s_render_page], s_frame.impedance) == 0 &&
        strcmp(s_reading_only_page_range[s_render_page], s_frame.range) == 0 &&
        strcmp(s_reading_only_page_rate[s_render_page], s_frame.rate) == 0 &&
        s_reading_only_page_info_lamps[s_render_page] == row2_info_lamps()) {
        /* Content identical: only the TRIGGER dot may need a repaint (blink
         * phase toggled). Single 8x8 fill, no full row redraw. */
        bool trig = s_frame.status_active[5u];
        int8_t want = !trig ? (int8_t)-1 : (s_trig_dot_phase ? (int8_t)1 : (int8_t)0);
        if (s_reading_only_page_trig_dot[s_render_page] == want) {
            s_trig_dot_pending = false;
            return true;
        }
        if (ui_fill_rect(row2_trig_dot_x(), row2_trig_dot_y(),
                         MAIN_DISPLAY_TRIG_DOT_SIZE, MAIN_DISPLAY_TRIG_DOT_SIZE,
                         want == 1 ? MAIN_DISPLAY_COLOR_GREEN :
                                     MAIN_DISPLAY_COLOR_BAR) != LT7680_OK) return false;
        s_reading_only_page_trig_dot[s_render_page] = want;
        s_trig_dot_pending = false;
        return true;
    }
    if (idx == 0u)
        s_dbg_info_repaint_window++;
    if (idx == 0u) {
        if (!page_valid && ui_fill_rect(0u, MAIN_DISPLAY_INFO_BAR_Y,
                                        MAIN_DISPLAY_UI_WIDTH,
                                        MAIN_DISPLAY_INFO_BAR_H,
                                        MAIN_DISPLAY_COLOR_BAR) != LT7680_OK)
            return false;
    }
    if (idx == 0u) { idx++; return false; }
    if (idx == 1u) {
        if (!function_dirty) { idx++; return false; }
        uint16_t fw = ui_measure_text(s_frame.function);
        /* Union erase: a narrower new badge must also cover the old badge's
         * tail, otherwise the old function text ghosts permanently (it only
         * repaints on the next function change). badge_w keeps the new
         * width for the text step below. */
        uint16_t clear_w;
        uint16_t old_w = 0u;
        if (page_valid)
        {
            old_w = ui_measure_text(
                s_reading_only_page_function[s_render_page]);
            old_w = (uint16_t)(old_w + 2u * MAIN_DISPLAY_BADGE_PAD_X);
            if (old_w < 80u) old_w = 80u;
            if (old_w > 220u) old_w = 220u;
        }
        badge_w = (uint16_t)(fw + 2u * MAIN_DISPLAY_BADGE_PAD_X);
        if (badge_w < 80u) badge_w = 80u;
        if (badge_w > 220u) badge_w = 220u;
        clear_w = old_w > badge_w ? old_w : badge_w;
        if (ui_fill_rect(MAIN_DISPLAY_BADGE_X, MAIN_DISPLAY_INFO_BAR_Y,
                         clear_w, MAIN_DISPLAY_INFO_BAR_H,
                         MAIN_DISPLAY_COLOR_BADGE_BG) != LT7680_OK) return false;
        idx++; return false;
    }
    if (idx == 2u) {
        if (!function_dirty) { idx++; return false; }
        uint16_t text_x = (uint16_t)(MAIN_DISPLAY_BADGE_X + MAIN_DISPLAY_BADGE_PAD_X);
        if (!ui_draw_text(text_x, row2_text_y(),
                          s_frame.function, MAIN_DISPLAY_COLOR_BADGE_TEXT)) return false;
        idx++; return false;
    }
    /* Stats-style cells in fixed slots (ADR-0007 row-2 grid): name on BAR,
     * value on BAR_ALT, 1px light separator at each block's fixed right
     * edge. 3 blocks x 4 sub-steps (idx 3..14). Value text may change;
     * positions never do, so a widened Range cannot push Rate/lamps. */
    if (idx >= 3u && idx <= 14u) {
        uint8_t b = (uint8_t)((idx - 3u) / 4u);
        uint8_t sub = (uint8_t)((idx - 3u) % 4u);
        const char *val = b == 0u ? s_frame.impedance :
                          b == 1u ? s_frame.range : s_frame.rate;
        uint16_t val_color = b == 0u ? MAIN_DISPLAY_COLOR_MUTED : MAIN_DISPLAY_COLOR_WHITE;
        uint16_t name_w = (uint16_t)(strlen(cell_names[b]) * FONT_TEXT_WIDTH +
                                     2u * MAIN_DISPLAY_ROW2_CELL_PAD_X);
        uint16_t val_zone = (uint16_t)(cell_w[b] - name_w);
        uint16_t cx = cell_x[b];
        if (sub == 0u) {
            if (!cell_dirty[b]) { idx++; return false; }
            if (ui_fill_rect((uint16_t)(cx + name_w), MAIN_DISPLAY_INFO_BAR_Y,
                             val_zone, MAIN_DISPLAY_INFO_BAR_H,
                             MAIN_DISPLAY_COLOR_BAR_ALT) != LT7680_OK) return false;
            idx++; return false;
        }
        if (sub == 1u) {
            if (page_valid && !cell_dirty[b]) { idx++; return false; }
            /* The value-zone fill never touches the name area: a
             * neighbour's spilled tail under the name would survive the
             * name redraw. Erase first — but ONLY when the shared bitmap
             * job is idle: on resume visits this erase would wipe the
             * already-drawn head while the job redraws just the tail
             * ("Ran" gone, "ge" stays). Same guard as the status bar. */
            if (!s_bitmap_job.active) {
                if (ui_fill_rect(cx, MAIN_DISPLAY_INFO_BAR_Y,
                                 name_w, MAIN_DISPLAY_INFO_BAR_H,
                                 MAIN_DISPLAY_COLOR_BAR) != LT7680_OK) return false;
            }
            if (!ui_draw_text((uint16_t)(cx + MAIN_DISPLAY_ROW2_CELL_PAD_X), row2_text_y(),
                              cell_names[b], MAIN_DISPLAY_COLOR_MUTED)) return false;
            idx++; return false;
        }
        if (sub == 2u) {
            if (!cell_dirty[b]) { idx++; return false; }
            /* Left-aligned by construction: fixed origin, never centered or
             * right-aligned; keep this x constant so overwrites stay exact. */
            if (!ui_draw_text((uint16_t)(cx + name_w + MAIN_DISPLAY_ROW2_CELL_PAD_X),
                              row2_text_y(), val, val_color)) return false;
            idx++; return false;
        }
        if (b == 2u) {
            /* Rate block: no full-height sep here; the section divider
             * between the RATE value and the lamp group is a half-height
             * short line (idx 15), same language as the row-1 brand sep. */
            idx++; return false;
        }
        /* Unconditional: the value-zone erase above covers this pixel, so
         * gating it on !page_valid ate the separator on every repaint. */
        if (ui_fill_rect((uint16_t)(cx + cell_w[b] - 1u), MAIN_DISPLAY_INFO_BAR_Y,
                         1u, MAIN_DISPLAY_INFO_BAR_H,
                         MAIN_DISPLAY_COLOR_SEP) != LT7680_OK) return false;
        idx++; return false;
    }
    if (idx == 15u) {
        if (ui_fill_rect(ROW2_X_SHORT_SEP,
                         (uint16_t)(MAIN_DISPLAY_INFO_BAR_Y +
                                    (MAIN_DISPLAY_INFO_BAR_H - MAIN_DISPLAY_ROW1_SEP_H) / 2u),
                         1u, MAIN_DISPLAY_ROW1_SEP_H,
                         MAIN_DISPLAY_COLOR_SEP) != LT7680_OK) return false;
        idx++; return false;
    }
    if (idx >= 16u && idx <= 18u) {
        /* Active-only right statuses in fixed slots: inactive lamps vanish
         * in place (no muted text, no neighbor shift), and must never
         * cross the trigger separator. */
        while (idx <= 18u && !s_frame.status_active[lamp_bits[idx - 16u]]) {
            uint8_t lamp = (uint8_t)(idx - 16u);
            uint8_t bit = lamp_bits[lamp];
            /* lamps_overflow also erases never-active slots: a spilled Rate
             * tail may sit on them and no lamp event will ever clean it. */
            if (page_valid &&
                (lamps_overflow ||
                 ((s_reading_only_page_info_lamps[s_render_page] & (1u << bit)) != 0u)) &&
                (s_frame.status_active[bit] == false))
            {
                if (ui_fill_rect(row2_lamp_x[lamp], MAIN_DISPLAY_INFO_BAR_Y,
                                 (uint16_t)(strlen(lamps[lamp]) * FONT_TEXT_WIDTH),
                                 MAIN_DISPLAY_INFO_BAR_H,
                                 MAIN_DISPLAY_COLOR_BAR) != LT7680_OK)
                    return false;
            }
            idx++;
        }
        if (idx > 18u) return false;
        {
            uint8_t lamp = (uint8_t)(idx - 16u);
            if (page_valid && !lamps_dirty)
            {
                idx++;
                return false;
            }
            uint16_t lx = row2_lamp_x[lamp];
            uint16_t end_x = (uint16_t)(lx + strlen(lamps[lamp]) * FONT_TEXT_WIDTH);
            if (end_x > row2_trig_sep_x()) { idx = 19u; return false; }
            /* Pre-erase on overflow frames: lamp text must not mix with a
             * spilled Rate tail underneath it. */
            if (lamps_overflow && page_valid &&
                ui_fill_rect(lx, MAIN_DISPLAY_INFO_BAR_Y,
                             (uint16_t)(strlen(lamps[lamp]) * FONT_TEXT_WIDTH),
                             MAIN_DISPLAY_INFO_BAR_H,
                             MAIN_DISPLAY_COLOR_BAR) != LT7680_OK) return false;
            if (!ui_draw_text(lx, row2_text_y(), lamps[lamp],
                              MAIN_DISPLAY_COLOR_GREEN)) return false;
            idx++; return false;
        }
    }
    if (idx == 19u) {
        /* TRIGGER block is always present; only its color/dot follow state. */
        if (ui_fill_rect(row2_trig_sep_x(), MAIN_DISPLAY_INFO_BAR_Y,
                         1u, MAIN_DISPLAY_INFO_BAR_H,
                         MAIN_DISPLAY_COLOR_SEP) != LT7680_OK) return false;
        idx++; return false;
    }
    if (idx == 20u) {
        if (page_valid &&
            ((s_reading_only_page_info_lamps[s_render_page] & 8u) != 0u) ==
                s_frame.status_active[5u]) { idx++; return false; }
        bool trig = s_frame.status_active[5u];
        if (!ui_draw_text(row2_trig_text_x(), row2_text_y(), "TRIGGER",
                          trig ? MAIN_DISPLAY_COLOR_GREEN :
                                 MAIN_DISPLAY_COLOR_MUTED)) return false;
        idx++; return false;
    }
    if (idx == 21u) {
        bool trig = s_frame.status_active[5u];
        bool dot_on = trig && s_trig_dot_phase;
        if (!trig)
        {
            /* Keep the dot area erased while TRIG is off. */
            if (ui_fill_rect(row2_trig_dot_x(), row2_trig_dot_y(),
                             MAIN_DISPLAY_TRIG_DOT_SIZE, MAIN_DISPLAY_TRIG_DOT_SIZE,
                             MAIN_DISPLAY_COLOR_BAR) != LT7680_OK) return false;
            s_reading_only_page_trig_dot[s_render_page] = (int8_t)-1;
            idx++; return false;
        }
        if (ui_fill_rect(row2_trig_dot_x(), row2_trig_dot_y(),
                         MAIN_DISPLAY_TRIG_DOT_SIZE, MAIN_DISPLAY_TRIG_DOT_SIZE,
                         dot_on ? MAIN_DISPLAY_COLOR_GREEN :
                                  MAIN_DISPLAY_COLOR_BAR) != LT7680_OK) return false;
        s_reading_only_page_trig_dot[s_render_page] = dot_on ? (int8_t)1 : (int8_t)0;
        idx++; return false;
    }
    if (idx == 22u) {
        if (ui_fill_rect(0u, MAIN_DISPLAY_INFO_BAR_Y + MAIN_DISPLAY_INFO_BAR_H,
                         MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_YELLOW_LINE_H,
                         MAIN_DISPLAY_COLOR_DIVIDER) != LT7680_OK) return false;
        idx++; return false;
    }
    strncpy(s_reading_only_page_function[s_render_page], s_frame.function, sizeof(s_reading_only_page_function[0]) - 1u);
    strncpy(s_reading_only_page_impedance[s_render_page], s_frame.impedance, sizeof(s_reading_only_page_impedance[0])-1u);
    strncpy(s_reading_only_page_range[s_render_page], s_frame.range, sizeof(s_reading_only_page_range[0])-1u);
    strncpy(s_reading_only_page_rate[s_render_page], s_frame.rate, sizeof(s_reading_only_page_rate[0])-1u);
    s_reading_only_page_function[s_render_page][sizeof(s_reading_only_page_function[0]) - 1u] = '\0';
    s_reading_only_page_impedance[s_render_page][sizeof(s_reading_only_page_impedance[0])-1u] = '\0';
    s_reading_only_page_range[s_render_page][sizeof(s_reading_only_page_range[0])-1u] = '\0';
    s_reading_only_page_rate[s_render_page][sizeof(s_reading_only_page_rate[0])-1u] = '\0';
    s_reading_only_page_info_lamps[s_render_page] = row2_info_lamps();
    s_reading_only_page_trig_dot[s_render_page] =
        s_frame.status_active[5u] ? (s_trig_dot_phase ? (int8_t)1 : (int8_t)0) : (int8_t)-1;
    s_trig_dot_pending = false;
    s_reading_only_page_info_valid[s_render_page] = true;
    idx = 0u;
    return true;
}

/* Trend header/taskbar text (ADR-0007): "±10V"/"100kΩ" range from the
 * resident axis, manual decimals (no float printf), DC/AC suffix stripped. */
static void trend_range_text(char *out, uint8_t size)
{
    float mag = s_trend_axis_max;
    const char *unit = s_trend_axis_unit;
    const char *pre = "";
    float v = mag;
    uint32_t h;
    uint8_t n = 0u;

    if (out == 0 || size == 0u) return;
    out[0] = '\0';
    /* NOTE: s_frame.trend_has_data is NOT valid here — main_display_format()
     * memsets the frame at every IDLE snapshot and the trend fields only
     * come back in the TREND stage. The buffer flag is the truth. Reading
     * it from the frame pins the row-2 range at "--" forever. */
    if (!s_trend.has_sample || !s_trend_axis_valid || !(mag > 0.0f))
    {
        if (size > 2u) memcpy(out, "--", 3u);
        return;
    }
    /* Temperature never takes an SI prefix ("k°C" is nonsense): keep the
     * raw magnitude with its native unit. has_pre is latched BEFORE the
     * `*pre++` emit loop below consumes the pointer — testing `*pre`
     * afterwards is always false (the strip silently never ran). */
    bool has_pre = false;
    if (strstr(unit, "\xC2\xB0") == 0)
    {
        if (v >= 1000000.0f) { v /= 1000000.0f; pre = "M"; }
        else if (v >= 1000.0f) { v /= 1000.0f; pre = "k"; }
    }
    has_pre = (*pre != '\0');
    h = (uint32_t)(v * 100.0f + 0.5f);
    if (s_trend_axis_min < 0.0f && n + 2u < size) { memcpy(&out[n], "\xC2\xB1", 2u); n += 2u; }
    {
        uint32_t whole = h / 100u;
        char rev[10];
        uint8_t rn = 0u;
        do { rev[rn++] = (char)('0' + whole % 10u); whole /= 10u; }
        while (whole != 0u && rn < (uint8_t)sizeof(rev));
        while (rn > 0u && n + 1u < size) out[n++] = rev[--rn];
    }
    if (h % 100u != 0u && n + 1u < size)
    {
        uint8_t d1 = (uint8_t)((h / 10u) % 10u);
        uint8_t d2 = (uint8_t)(h % 10u);
        out[n++] = '.';
        if (n + 1u < size) out[n++] = (char)('0' + d1);
        if (d2 != 0u && n + 1u < size) out[n++] = (char)('0' + d2);
    }
    while (*pre != '\0' && n + 1u < size) out[n++] = *pre++;
    {
        const char *u = unit;
        size_t ulen = strlen(unit);
        size_t i;
        if (ulen >= 2u &&
            ((unit[ulen - 2u] == 'D' && unit[ulen - 1u] == 'C') ||
             (unit[ulen - 2u] == 'A' && unit[ulen - 1u] == 'C')))
            ulen -= 2u;
        /* The magnitude already carries the SI step (pre): a unit with its
         * own prefix would print it twice ("5kkHz", "10MMΩ", "500kMHz").
         * Drop the unit's prefix only when pre is engaged; small magnitudes
         * keep their native unit ("40mV", "500kHz"). Micro steps down to
         * milli, the rest strip to the bare stem. */
        if (has_pre && ulen > 1u &&
            (u[0] == 'm' || u[0] == 'k' || u[0] == 'M'))
        {
            u++; ulen--;
        }
        else if (has_pre && u[0] == 'u' && ulen > 1u)
        {
            if (n + 1u < size) out[n++] = 'm';
            u++; ulen--;
        }
        else if (has_pre && ulen > 2u &&
                 (uint8_t)u[0] == 0xC2u && (uint8_t)u[1] == 0xB5u)
        {
            if (n + 1u < size) out[n++] = 'm';
            u += 2u; ulen -= 2u;
        }
        for (i = 0u; i < ulen && n + 1u < size; i++) out[n++] = u[i];
    }
    out[n] = '\0';
}

static uint16_t trend_grid_x(uint8_t gi)
{
    return (uint16_t)(MAIN_DISPLAY_PLOT_X +
                      (uint32_t)gi * MAIN_DISPLAY_PLOT_W / 4u);
}

/* Fixed header slot k (0=MAX left, 1=AVG mid, 2=MIN right): "MAX <v><unit>".
 * Values come from the shared snapshot (see s_trend_stat_snapshot), never
 * straight from the sliding window — otherwise the two pages mint different
 * last digits one frame apart and every flip flickers. */
static void trend_header_slot(uint8_t k, char *out, uint8_t size)
{
    uint8_t n = 0u;
    const char *p;

    if (out == 0 || size == 0u) return;
    out[0] = '\0';
    if (k >= 3u || !s_trend_stat_snap_valid) return;
    for (p = s_trend_stat_snapshot[k]; *p != '\0' && n + 1u < size; p++)
        out[n++] = *p;
    out[n] = '\0';
}

/* Fresh slot string k from the live frame (unit included). */
static void trend_live_cell(uint8_t k, char *out, uint8_t size)
{
    static const char *const tags[3] = {"MAX ", "AVG ", "MIN "};
    const char *vals[3];
    uint8_t n = 0u;
    const char *p;

    if (out == 0 || size == 0u) return;
    out[0] = '\0';
    if (k >= 3u) return;
    vals[0] = s_frame.trend_has_data ? s_frame.trend_stat_maximum_text : "--";
    vals[1] = s_frame.trend_has_data ? s_frame.trend_stat_average_text : "--";
    vals[2] = s_frame.trend_has_data ? s_frame.trend_stat_minimum_text : "--";
    for (p = tags[k]; *p != '\0' && n + 1u < size; p++) out[n++] = *p;
    for (p = vals[k]; *p != '\0' && n + 1u < size; p++) out[n++] = *p;
    out[n] = '\0';
}

/* Capture the slot snapshot from the live frame (unit included). Called
 * once per background build, before the slot steps paint from it. */
static void trend_stat_snapshot_capture(void)
{
    uint8_t k;
    for (k = 0u; k < 3u; k++)
        trend_live_cell(k, s_trend_stat_snapshot[k],
                        sizeof(s_trend_stat_snapshot[k]));
    s_trend_stat_snap_valid = true;
}

/* Per-glyph diff repaint of one slot (fixed 12 px advance,
 * multibyte-aware via text_glyph). Only changed glyph cells are erased
 * (exactly the 24 px content band) + redrawn with a throwaway job — never
 * the shared resumable job, so many glyphs may complete in one visit, and
 * static chrome (BAR/badge/Trend) is never touched: no 1 Hz full-header
 * blink. Snapshot updated on success. */


/* Budgeted per-glyph diff: paints at most `budget` changed glyphs per call
 * (each erase+draw ≈ 9 GE fills ≈ 3 ms single-page), resuming at *pos next
 * visit. Live uses budget 4 (≈36 fills ≈ ≤15 ms/visit worst case).
 * Throwaway job per glyph — never touches the shared resumable job, so a
 * suspend/resume or abort mid-cell cannot corrupt it. Snapshot written only
 * on full completion; callers must reset *pos when starting a new string. */
static bool trend_paint_cell_diff(const char *old_text, const char *new_text,
                                  uint16_t x, uint16_t y, uint16_t h, uint16_t color,
                                  char *saved, uint8_t saved_size,
                                  uint8_t *pos, uint8_t budget)
{
    const char *op = old_text != 0 ? old_text : "";
    const char *np = new_text != 0 ? new_text : "";
    uint16_t gi = 0u;
    uint8_t painted = 0u;

    while (gi < *pos && (*op != '\0' || *np != '\0'))
    {
        uint8_t oa = 1u;
        uint8_t na = 1u;
        if (*op != '\0') (void)text_glyph(op, &oa);
        if (*np != '\0') (void)text_glyph(np, &na);
        if (*op != '\0') op += oa;
        if (*np != '\0') np += na;
        gi++;
    }
    while (*op != '\0' || *np != '\0')
    {
        uint8_t oa = 1u;
        uint8_t na = 1u;
        char token[3] = {0, 0, 0};
        bitmap_job_t job = {0};
        uint8_t k;

        if (*op != '\0') (void)text_glyph(op, &oa);
        if (*np != '\0') (void)text_glyph(np, &na);
        if (!(oa == na && oa != 0u && memcmp(op, np, oa) == 0))
        {
            uint16_t gx = (uint16_t)(x + gi * FONT_TEXT_WIDTH);
            if (ui_fill_rect(gx, y, FONT_TEXT_WIDTH, h,
                             MAIN_DISPLAY_COLOR_BAR) != LT7680_OK)
                return false;
            if (*np != '\0')
            {
                for (k = 0u; k < na && k < 2u; k++) token[k] = np[k];
                if (!ui_draw_bitmap_slice(&job, gx, y, token, color, 0u))
                    return false;
            }
            if (++painted >= budget)
            {
                *pos = (uint8_t)(gi + 1u);
                return false;
            }
        }
        if (*op != '\0') op += oa;
        if (*np != '\0') np += na;
        gi++;
        if (gi >= 20u) break;
    }
    {
        uint8_t n = 0u;
        while (new_text != 0 && new_text[n] != '\0' && n + 1u < saved_size)
        {
            saved[n] = new_text[n];
            n++;
        }
        saved[n] = '\0';
    }
    *pos = 0u;
    return true;
}

static uint32_t s_trend_live_tick;

/* Live refresh due: snapshot exists, 1 s elapsed, fresh strings differ.
 * Pure-RAM compare when clean. Deliberately NOT faster: at 500 Hz input
 * the window stats differ on every pass, so cadence only sets how often
 * a ~25 ms repaint happens — 10 Hz burns 250 ms/s in hitches, 1 Hz burns
 * 25 ms/s for one stretched frame. Measured live=25~30 ms/visit pre-fix. */
static bool trend_live_due(uint32_t now)
{
    uint8_t k;
    char tmp[24];

    if (!s_trend_stat_snap_valid) return false;
    if ((uint32_t)(now - s_trend_live_tick) < 1000u) return false;
    for (k = 0u; k < 3u; k++)
    {
        trend_live_cell(k, tmp, sizeof(tmp));
        if (strcmp(tmp, s_trend_stat_snapshot[k]) != 0) return true;
    }
    return false;
}

/* Live stat refresh: changed glyphs only (≤2 per visit). One slot per
 * visit, and the caller does NOT wait for pass completion — it presents
 * every frame and resumes next frame. That removes the stall-then-recover
 * pattern: the reading commits metronomically while stats chase within
 * ~3 frames. Snapshot publishes per completed slot.
 * Dual-page scoped to the paint CALL (never the pass): both pages get
 * identical digits every visit (no flip flicker), while the global gate
 * is already clear again before any unrelated STATUS/INFO/VALUE stage
 * runs — a pass-scoped gate leaked dual writes across compositions and
 * rows visibly painted item-by-item; a single-page pass left the pages
 * with complementary slots (two spectra alternating). */
static bool reading_only_render_trend_live(void)
{
    static uint8_t idx;
    static uint8_t diff_slot = 0xFFu;
    static uint8_t diff_pos;
    uint16_t ty = (uint16_t)(MAIN_DISPLAY_TREND_HEADER_Y + MAIN_DISPLAY_YELLOW_LINE_H);

    if (idx <= 2u)
    {
        uint8_t k = (uint8_t)idx;
        uint16_t x = (uint16_t)(MAIN_DISPLAY_TREND_STAT_X0 +
                                (uint16_t)k * MAIN_DISPLAY_TREND_STAT_PITCH);
        char text[24];
        bool ok;
        if (k != diff_slot) { diff_slot = k; diff_pos = 0u; }
        trend_live_cell(k, text, sizeof(text));
        s_trend_sweep_drawing = true;
        ok = trend_paint_cell_diff(s_trend_stat_snapshot[k], text, x, ty,
                                   (uint16_t)(MAIN_DISPLAY_TREND_HEADER_H - MAIN_DISPLAY_YELLOW_LINE_H),
                                   MAIN_DISPLAY_COLOR_WHITE,
                                   s_trend_stat_snapshot[k],
                                   sizeof(s_trend_stat_snapshot[k]),
                                   &diff_pos, 2u);
        s_trend_sweep_drawing = false;
        if (!ok)
        {
            if (s_reading_only_io_error) idx = 0u;
            return false;
        }
        idx++;
        return false;
    }
    s_trend_live_tick = HAL_GetTick();
    idx = 0u;
    return true;
}

/* Elapsed-time label for gridline gi: window fractions 1..0 ("10s".."0s"). */
static void trend_time_text(uint8_t gi, char *out, uint8_t size)
{
    uint32_t tenths = (uint32_t)((uint32_t)TREND_WINDOW_MS * (4u - gi) / 4u / 100u);
    uint32_t whole = tenths / 10u;
    uint32_t frac = tenths % 10u;
    uint8_t n = 0u;

    if (out == 0 || size == 0u) return;
    {
        char rev[6];
        uint8_t rn = 0u;
        do { rev[rn++] = (char)('0' + whole % 10u); whole /= 10u; }
        while (whole != 0u && rn < (uint8_t)sizeof(rev));
        while (rn > 0u && n + 1u < size) out[n++] = rev[--rn];
    }
    if (frac != 0u && n + 2u < size)
    {
        out[n++] = '.';
        out[n++] = (char)('0' + frac);
    }
    if (n + 1u < size) out[n++] = 's';
    out[n] = '\0';
}

/* Redraw the vertical indicator lines crossing [x0,x1] (ADR-0007): slot
 * erases punch holes in them, so every erase is followed by a restore. */
static bool trend_sweep_restore_verticals(uint16_t x0, uint16_t x1)
{
    uint8_t gi;
    for (gi = 0u; gi < MAIN_DISPLAY_TREND_GRID_COUNT; gi++)
    {
        uint16_t gx = trend_grid_x(gi);
        if (gx < x0 || gx > x1) continue;
        if (ui_draw_line(gx, MAIN_DISPLAY_PLOT_Y,
                         gx, (uint16_t)(MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H - 1u),
                         MAIN_DISPLAY_COLOR_GRID) != LT7680_OK)
            return false;
    }
    return true;
}

/* Bottom band composition (ADR-0007): header bar (Trend + range + stats),
 * black plot with vertical indicator lines, taskbar with elapsed times. */
static bool reading_only_render_trend_background(void)
{
    static uint8_t idx;
    static uint8_t last_page = 0xFFu;
    static char pending_unit[TREND_UNIT_ID_MAX];
    const char *unit = trend_buffer_display_unit(&s_trend);

    if (last_page != s_render_page)
    {
        idx = 0u;
        pending_unit[0] = '\0';
        last_page = s_render_page;
    }
    if (strcmp(pending_unit, unit) != 0)
    {
        idx = 0u;
        strncpy(pending_unit, unit, TREND_UNIT_ID_MAX - 1u);
        pending_unit[TREND_UNIT_ID_MAX - 1u] = '\0';
    }
    /* These helpers remain part of the non-baseline renderer; keep their
     * definitions live in the baseline build as well. */
    (void)trend_draw_background;
    (void)trend_restore_grid;
    if (s_reading_only_page_trend_bg_valid[s_render_page] &&
        strcmp(s_reading_only_page_trend_unit[s_render_page], unit) == 0)
    {
        return true;
    }
    /* Empty identity right after a rotation reset: the buffer was just
     * cleared and no new-unit sample has landed yet. Painting now would
     * store "" as the page unit and force a second full rebuild one frame
     * later when the identity arrives (the ~2000-fill rotation storm).
     * Wait it out (~2 ms) — unless virgin (boot, cached unit also empty),
     * which must still build taskbar/verticals/badge once. */
    if (unit[0] == '\0' &&
        s_reading_only_page_trend_unit[s_render_page][0] != '\0')
    {
        return true;
    }
    /* First page to rebuild captures the stat snapshot; the sibling
     * repaints from it one frame later — identical digits, no flicker. */
    if (!s_trend_stat_snap_valid)
        trend_stat_snapshot_capture();
    /* Virgin page (boot, cached unit empty): full build. Rotation (stale
     * cached unit): targeted repaint — badge/line/Trend/verticals/taskbar
     * are static and survive; only plot + dynamic text strips refresh. */
    bool virgin = s_reading_only_page_trend_unit[s_render_page][0] == '\0';

    if (idx == 0u)
    {
        if (virgin)
        {
            /* Header + taskbar share the BAR backdrop; the plot is black. */
            if (ui_fill_rect(0u, MAIN_DISPLAY_TREND_Y,
                             MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_TREND_H,
                             MAIN_DISPLAY_COLOR_BAR) != LT7680_OK)
            {
                if (s_reading_only_io_error) idx = 0u;
                return false;
            }
        }
        else
        {
            /* Targeted erases: header content right of the badge + gutter.
             * Badge, line, Trend text, verticals and taskbar stay. */
            if (ui_fill_rect(MAIN_DISPLAY_TREND_BADGE_W,
                             (uint16_t)(MAIN_DISPLAY_TREND_HEADER_Y + MAIN_DISPLAY_YELLOW_LINE_H),
                             (uint16_t)(MAIN_DISPLAY_UI_WIDTH - MAIN_DISPLAY_TREND_BADGE_W),
                             (uint16_t)(MAIN_DISPLAY_TREND_HEADER_H - MAIN_DISPLAY_YELLOW_LINE_H),
                             MAIN_DISPLAY_COLOR_BAR) != LT7680_OK)
            {
                if (s_reading_only_io_error) idx = 0u;
                return false;
            }
            if (ui_fill_rect(0u, MAIN_DISPLAY_PLOT_Y,
                             MAIN_DISPLAY_TREND_GUTTER_W, MAIN_DISPLAY_PLOT_H,
                             MAIN_DISPLAY_COLOR_BAR) != LT7680_OK)
            {
                if (s_reading_only_io_error) idx = 0u;
                return false;
            }
        }
        idx = 1u;
        return false;
    }
    if (idx == 1u)
    {
        if (ui_fill_rect(MAIN_DISPLAY_PLOT_X, MAIN_DISPLAY_PLOT_Y,
                         MAIN_DISPLAY_PLOT_W, MAIN_DISPLAY_PLOT_H,
                         MAIN_DISPLAY_COLOR_BG) != LT7680_OK)
        {
            if (s_reading_only_io_error) idx = 0u;
            return false;
        }
        idx = 2u;
        return false;
    }
    if (idx == 2u)
    {
        uint8_t gi;
        if (!virgin) { idx = 5u; return false; }
        for (gi = 0u; gi < MAIN_DISPLAY_TREND_GRID_COUNT; gi++)
        {
            if (ui_draw_line(trend_grid_x(gi), MAIN_DISPLAY_PLOT_Y,
                             trend_grid_x(gi),
                             (uint16_t)(MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H - 1u),
                             MAIN_DISPLAY_COLOR_GRID) != LT7680_OK)
            {
                if (s_reading_only_io_error) idx = 0u;
                return false;
            }
        }
        idx = 3u;
        return false;
    }
    if (idx == 3u)
    {
        /* Green line on the Trend TOP edge; badge + texts live in the
         * content band below it (line first, badge over it). */
        if (ui_fill_rect(0u, MAIN_DISPLAY_TREND_HEADER_Y,
                         MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_YELLOW_LINE_H,
                         MAIN_DISPLAY_COLOR_DIVIDER) != LT7680_OK)
        {
            if (s_reading_only_io_error) idx = 0u;
            return false;
        }
        if (ui_fill_rect(0u,
                         (uint16_t)(MAIN_DISPLAY_TREND_HEADER_Y + MAIN_DISPLAY_YELLOW_LINE_H),
                         MAIN_DISPLAY_TREND_BADGE_W,
                         (uint16_t)(MAIN_DISPLAY_TREND_HEADER_H - MAIN_DISPLAY_YELLOW_LINE_H),
                         MAIN_DISPLAY_COLOR_BADGE_BG) != LT7680_OK)
        {
            if (s_reading_only_io_error) idx = 0u;
            return false;
        }
        idx++;
        return false;
    }
    if (idx == 4u)
    {
        uint16_t ty = (uint16_t)(MAIN_DISPLAY_TREND_HEADER_Y + MAIN_DISPLAY_YELLOW_LINE_H);
        if (!ui_draw_text(MAIN_DISPLAY_BADGE_PAD_X, ty,
                          "Trend", MAIN_DISPLAY_COLOR_BADGE_TEXT)) return false;
        idx++;
        return false;
    }
    if (idx >= 5u && idx <= 7u)
    {
        /* Fixed left/mid/right slots, one string per step (single job
         * rule): "MAX <bare>" etc. in white. Positions never move, so the
         * auto-squeeze jitter of the sequential layout is gone. */
        uint8_t k = (uint8_t)(idx - 5u);
        uint16_t x = (uint16_t)(MAIN_DISPLAY_TREND_STAT_X0 +
                                (uint16_t)k * MAIN_DISPLAY_TREND_STAT_PITCH);
        uint16_t ty = (uint16_t)(MAIN_DISPLAY_TREND_HEADER_Y + MAIN_DISPLAY_YELLOW_LINE_H);
        char text[16];
        trend_header_slot(k, text, sizeof(text));
        if (!ui_draw_text(x, ty, text, MAIN_DISPLAY_COLOR_WHITE)) return false;
        idx++;
        return false;
    }
    if (idx >= 8u && idx <= 10u)
    {
        /* Left Y gutter: max / mid / min of the resident axis. */
        uint8_t i = (uint8_t)(idx - 8u);
        size_t label_width = strlen(s_frame.y_labels[i]) * FONT_TEXT_WIDTH;
        uint16_t label_x = label_width + 4u <= MAIN_DISPLAY_TREND_GUTTER_W
                               ? (uint16_t)(MAIN_DISPLAY_TREND_GUTTER_W - 4u - (uint16_t)label_width)
                               : 0u;
        uint16_t axis_y = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     i * MAIN_DISPLAY_PLOT_H / 2u);
        uint16_t label_y = axis_y > MAIN_DISPLAY_PLOT_Y + FONT_TEXT_HEIGHT / 2u
                               ? (uint16_t)(axis_y - FONT_TEXT_HEIGHT / 2u)
                               : MAIN_DISPLAY_PLOT_Y;
        uint16_t bottom = (uint16_t)(MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H -
                                     FONT_TEXT_HEIGHT);
        if (label_y > bottom) label_y = bottom;
        if (s_frame.y_labels[i][0] != '\0' &&
            !ui_draw_text(label_x, label_y, s_frame.y_labels[i],
                          MAIN_DISPLAY_COLOR_CYAN))
        {
            if (s_reading_only_io_error) idx = 0u;
            return false;
        }
        idx++;
        return false;
    }
    if (idx == 11u)
    {
        /* Empty window hint; a live window skips this step silently. */
        if (!s_frame.trend_has_data)
        {
            const char *hint = "WAITING FOR DATA";
            uint16_t hx = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                                     (MAIN_DISPLAY_PLOT_W - strlen(hint) * FONT_TEXT_WIDTH) / 2u);
            uint16_t hy = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                     (MAIN_DISPLAY_PLOT_H - FONT_TEXT_HEIGHT) / 2u);
            if (!ui_draw_text(hx, hy, hint, MAIN_DISPLAY_COLOR_MUTED))
            {
                if (s_reading_only_io_error) idx = 0u;
                return false;
            }
        }
        idx++;
        return false;
    }
    if (idx >= 12u && idx <= 16u)
    {
        /* Taskbar elapsed times under their gridlines, "0s" at newest.
         * Static per window geometry — rotation rebuilds skip them. */
        uint8_t gi;
        if (!virgin) { idx = 17u; return false; }
        gi = (uint8_t)(idx - 12u);
        char label[8];
        uint16_t gx = trend_grid_x(gi);
        uint16_t lx;
        trend_time_text(gi, label, sizeof(label));
        lx = (uint16_t)(gx - strlen(label) * FONT_TEXT_WIDTH / 2u);
        if (lx < MAIN_DISPLAY_PLOT_X) lx = MAIN_DISPLAY_PLOT_X;
        if (lx + strlen(label) * FONT_TEXT_WIDTH >
            MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W)
            lx = (uint16_t)(MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W -
                            strlen(label) * FONT_TEXT_WIDTH);
        if (!ui_draw_text(lx, MAIN_DISPLAY_TREND_TASKBAR_Y, label,
                          MAIN_DISPLAY_COLOR_CYAN)) return false;
        idx++;
        return false;
    }
    strncpy(s_reading_only_page_trend_unit[s_render_page], unit,
            TREND_UNIT_ID_MAX - 1u);
    s_reading_only_page_trend_unit[s_render_page][TREND_UNIT_ID_MAX - 1u] = '\0';
    memset(s_drawn_trend_occupied[s_render_page], 0,
           sizeof(s_drawn_trend_occupied[0]));
    s_reading_only_page_trend_curve_valid[s_render_page] = false;
    s_reading_only_page_trend_bg_valid[s_render_page] = true;
    idx = 0u;
    return true;
}

/* ADR-0006: trend stats column removed with the plot-only bottom band. */

static void keithley_trend_axis_range(const char *unit, float peak,
                                      float *minimum, float *maximum)
{
    static const float voltage[] = {0.1f, 1.0f, 10.0f, 100.0f, 1000.0f};
    static const float ac_voltage[] = {0.1f, 1.0f, 10.0f, 100.0f, 750.0f};
    static const float current[] = {0.01f, 0.1f, 1.0f, 3.0f};
    static const float ac_current[] = {1.0f, 3.0f};
    static const float resistance[] = {100.0f, 1000.0f, 10000.0f,
                                       100000.0f, 1000000.0f, 10000000.0f,
                                       100000000.0f};
    const float *levels = voltage;
    uint8_t count = (uint8_t)(sizeof(voltage) / sizeof(voltage[0]));
    uint8_t i;
    float full_scale;

    *minimum = 0.0f;
    *maximum = voltage[0];
    if (unit != 0 && strstr(unit, "VAC") != 0)
    {
        levels = ac_voltage;
        count = (uint8_t)(sizeof(ac_voltage) / sizeof(ac_voltage[0]));
    }
    else if (unit != 0 && (strstr(unit, "ADC") != 0 ||
                           strstr(unit, "mA") != 0))
    {
        levels = current;
        count = (uint8_t)(sizeof(current) / sizeof(current[0]));
    }
    else if (unit != 0 && strstr(unit, "AAC") != 0)
    {
        levels = ac_current;
        count = (uint8_t)(sizeof(ac_current) / sizeof(ac_current[0]));
    }
    else if (unit != 0 && (strstr(unit, "OHM") != 0 ||
                           strstr(unit, "Ohm") != 0 ||
                           strstr(unit, "\xCE\xA9") != 0))
    {
        levels = resistance;
        count = (uint8_t)(sizeof(resistance) / sizeof(resistance[0]));
        *minimum = 0.0f;
    }
    else if (unit != 0 && (strstr(unit, "CEL") != 0 ||
                           strstr(unit, "\xC2\xB0") != 0))
    {
        *minimum = -200.0f;
        *maximum = 1372.0f;
        return;
    }
    else if (unit != 0 && strstr(unit, "Hz") != 0)
    {
        *minimum = 0.0f;
        *maximum = 500000.0f;
        return;
    }
    full_scale = levels[count - 1u];
    for (i = 0u; i < count; i++)
        if (peak <= levels[i])
        {
            full_scale = levels[i];
            break;
        }
    if (*minimum == 0.0f && strstr(unit, "OHM") == 0 &&
        strstr(unit, "Ohm") == 0 && strstr(unit, "\xCE\xA9") == 0)
        *minimum = -full_scale;
    *maximum = full_scale;
}

static void internal_temperature_init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    ADC1->CR2 = ADC_CR2_TSVREFE;
    ADC1->SMPR1 = (7u << 18u) | (7u << 21u);
    ADC1->SQR1 = 0u;
    ADC1->SQR3 = 16u;
    ADC1->CR2 |= ADC_CR2_ADON;
    HAL_Delay(1u);
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0u) {}
    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0u) {}
}

static int16_t internal_temperature_read(void)
{
    uint32_t raw;
    uint16_t ts_cal1 = *(const uint16_t *)0x1FFFF7B8u;
    uint16_t ts_cal2 = *(const uint16_t *)0x1FFFF7C2u;

    ADC1->SQR3 = 16u;
    ADC1->CR2 |= ADC_CR2_ADON;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while ((ADC1->SR & ADC_SR_EOC) == 0u) {}
    raw = ADC1->DR;
    if (raw == 0u || ts_cal2 <= ts_cal1)
        return INT16_MIN;
    return (int16_t)(300 + ((int32_t)raw - (int32_t)ts_cal1) * 800 /
                     ((int32_t)ts_cal2 - (int32_t)ts_cal1));
}

static void reading_only_render(void)
{
    uint32_t now = HAL_GetTick();

    if (!s_display_ready)
        return;
    {
        uint8_t cur = (uint8_t)s_reading_only_stage;
        if (!s_dbg_stage_armed)
        {
            s_dbg_stage_armed = true;
        }
        else if (cur < 9u && s_dbg_last_stage < 9u)
        {
            uint32_t dt = now - s_dbg_last_render_tick;
            if (dt < 5000u)
            {
                s_dbg_stage_ms_window[s_dbg_last_stage] += dt;
                if (dt > s_dbg_stage_max_window[s_dbg_last_stage])
                    s_dbg_stage_max_window[s_dbg_last_stage] = dt;
            }
        }
        s_dbg_last_stage = cur < 9u ? cur : 0u;
        s_dbg_last_render_tick = now;
    }
    if (s_reading_only_stage == READING_ONLY_STATUS) {
        if (!reading_only_render_status_bar()) {
            if (s_reading_only_io_error)
                s_reading_only_stage = READING_ONLY_CLEAR;
            return;
        }
        if (s_is_header_only) {
            // Header-only via hidden page: keep reading band synced, present after header
            s_reading_only_stage = READING_ONLY_PRESENT;
            s_is_header_only = false;
            return;
        }
        {
               bool info_need = s_trig_dot_pending ||
                                !s_reading_only_page_info_valid[s_render_page] ||
                                strcmp(s_reading_only_page_function[s_render_page], s_frame.function) != 0 ||
                                strcmp(s_reading_only_page_impedance[s_render_page], s_frame.impedance) != 0 ||
                                strcmp(s_reading_only_page_range[s_render_page], s_frame.range) != 0 ||
                                strcmp(s_reading_only_page_rate[s_render_page], s_frame.rate) != 0 ||
                                s_reading_only_page_info_lamps[s_render_page] != row2_info_lamps();
            /* Rows paint inside the transaction on the hidden page and ride
             * the same atomic flip (PRESENT is already gated): freezing them
             * here only delays rows one composition behind the reading and
             * manufactures the mixed page (new reading + old rows). */
            s_reading_only_stage = info_need ? READING_ONLY_INFO
                                             : READING_ONLY_CLEAR;
        }
        return;
    }
    if (s_reading_only_stage == READING_ONLY_INFO) {
        if (!reading_only_render_info_panel()) {
            if (s_reading_only_io_error)
                s_reading_only_stage = READING_ONLY_CLEAR;
            return;
        }
        if (s_deferred_row_pending)
        {
            s_deferred_row_pending = false;
            s_reading_only_stage = READING_ONLY_PRESENT;
        }
        else
        {
            s_reading_only_stage = READING_ONLY_CLEAR;
        }
        return;
    }
    if (s_reading_only_stage == READING_ONLY_IDLE)
    {
        bool header_due = (uint32_t)(now - s_temperature_tick) >= 1000u;
        bool reading_due = s_reading_only_dirty &&
                           (uint32_t)(now - s_display_due_tick) >= DISPLAY_FRAME_PERIOD_MS;
        bool dot_due = s_trig_dot_pending &&
                       (uint32_t)(now - s_display_due_tick) >= DISPLAY_FRAME_PERIOD_MS;
        if (!reading_due && !header_due && !dot_due)
            return;
        /* Header-only: still use hidden page + present to avoid visible tear, but copy reading band. */
        /* No header-only presents before the first full reading composition:
         * at boot that path commits a top-rows-only page while the virgin
         * pipeline is still building (visible stall on two rows). */
        bool header_only = header_due && !reading_due && !dot_due &&
                           s_reading_only_frame_generation != 0u;
        s_is_header_only = header_only;

        /* A/B: the direct-DMA diagnostic holds the selected page to determine
         * whether the variable black region is stale content exposed at a page
         * flip. The normal baseline continues to alternate hidden pages. */
        s_render_page = READING_ONLY_PAGE_FLIP
                            ? (uint8_t)(s_visible_page ^ 1u)
                            : s_visible_page;
        if (header_only) {
            lt7680_rect_t sync_rect;
            panel_transform_ui_rect_to_fb(0u, MAIN_DISPLAY_READING_Y,
                                          MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_READING_H,
                                          &sync_rect.x, &sync_rect.y, &sync_rect.w, &sync_rect.h);
            (void)lt7680_gfx_copy_rect(s_visible_page, s_render_page, &sync_rect);
        }
        /* Never repaint the scanned page: doing so makes the header flash
         * during trend updates. */
        s_renderer.phase = RENDER_PHASE_UPDATE_READING;
        s_frame_rendering = true;
        s_render_full_page = false;
#if RIF_BTE_RENDERER
        if (!header_only) {
            s_reading_diff = false;
            s_cell_count = 0u;
            s_prev_reading_color = 0xFFFFu;
            s_prev_reading_nodata = 0xFFu;
            s_prev_suffix[0] = '\0';
            s_prev_suffix_x = 0u;
            s_prev_suffix_color = 0u;
        }
#endif
        main_display_format(&s_ui, &s_frame);
        refresh_runtime_snapshot();
        /* Row-2 Range cell shows mode + resident range ("AUTO ±10V");
         * the trend header keeps MAX/AVG/MIN only. Content-driven repaint
         * comes free via the existing range string compare. */
        {
            char range_text[16];
            size_t rl = strlen(s_frame.range);
            trend_range_text(range_text, sizeof(range_text));
            if (rl + 1u + strlen(range_text) < sizeof(s_frame.range))
            {
                size_t i = 0u;
                s_frame.range[rl] = ' ';
                while (range_text[i] != '\0')
                {
                    s_frame.range[rl + 1u + i] = range_text[i];
                    i++;
                }
                s_frame.range[rl + 1u + i] = '\0';
            }
        }
        if (!header_only) {
            s_reading_only_frame_generation = s_reading_only_generation;
            s_reading_only_value_index = 0u;
        } else {
            s_reading_only_value_index = s_frame.value_len;
        }
        s_reading_only_io_error = false;
        s_perf_frame_start_tick = now;
        if (!header_only) s_display_due_tick = now;
        /* Row snapshot lifecycle (see decl): hold one coherent row state
         * for the episode; live again once quiet. */
        {
            uint32_t behind =
                (s_trend.has_sample &&
                 s_trend.newest_bucket > s_sweep_cursor_bucket)
                    ? s_trend.newest_bucket - s_sweep_cursor_bucket
                    : 0u;
            /* Slow storm flips (> 100 ms) extend the episode: re-anchor
             * snaps `behind` to zero, so backlog alone cannot hold it, and
             * live churn would alternate across pages visibly. The frame-ms
             * gate only extends an existing snapshot (taking still needs a
             * transaction), so it cannot freeze steady-state rows. 5 s cap
             * against freezing rows forever in a pathological regime. */
            bool row_episode = s_trend_rebuild_transaction || behind > 8u ||
                               s_perf_last_frame_ms > 100u;
            if (!row_episode)
            {
                s_row_snap_taken = false;
                s_row_snap_range_taken = false;
            }
            else
            {
                if (s_row_snap_taken &&
                    (uint32_t)(now - s_row_snap_tick) >= 5000u)
                {
                    s_row_snap_taken = false;
                    s_row_snap_range_taken = false;
                }
                if (s_trend_rebuild_transaction && !s_row_snap_taken &&
                    s_ui.any_message && s_frame.temperature[0] != '\0' &&
                    s_frame.uptime[0] != '\0')
                {
                    uint8_t i;
                    for (i = 0u; i < STATUS_BAR_CORE_COUNT; i++)
                        s_row_snap_active[i] = s_frame.status_active[i];
                    s_row_snap_shift = s_frame.shift_active;
                    s_row_snap_rear = s_frame.rear_active;
                    memcpy(s_row_snap_rate, s_frame.rate,
                           sizeof(s_row_snap_rate));
                    memcpy(s_row_snap_temperature, s_frame.temperature,
                           sizeof(s_row_snap_temperature));
                    memcpy(s_row_snap_uptime, s_frame.uptime,
                           sizeof(s_row_snap_uptime));
                    s_row_snap_taken = true;
                    s_row_snap_tick = now;
                }
                if (!s_row_snap_range_taken && s_trend_axis_valid)
                {
                    memcpy(s_row_snap_range, s_frame.range,
                           sizeof(s_row_snap_range));
                    s_row_snap_range_taken = true;
                }
                if (s_row_snap_taken)
                {
                    uint8_t i;
                    for (i = 0u; i < STATUS_BAR_CORE_COUNT; i++)
                        s_frame.status_active[i] = s_row_snap_active[i];
                    s_frame.shift_active = s_row_snap_shift;
                    s_frame.rear_active = s_row_snap_rear;
                    memcpy(s_frame.rate, s_row_snap_rate,
                           sizeof(s_frame.rate));
                    memcpy(s_frame.temperature, s_row_snap_temperature,
                           sizeof(s_frame.temperature));
                    memcpy(s_frame.uptime, s_row_snap_uptime,
                           sizeof(s_frame.uptime));
                }
                if (s_row_snap_range_taken)
                {
                    memcpy(s_frame.range, s_row_snap_range,
                           sizeof(s_frame.range));
                }
            }
        }
        {
            uint8_t cur_status = 0u;
            for (uint8_t i = 0u; i < 5u; i++) if (s_frame.status_active[(uint8_t[]){0u,1u,2u,3u,5u}[i]]) cur_status |= (1u<<i);
             char cur_row1[MAIN_DISPLAY_META_MAX];
             bool status_need;
             row1_status_text(cur_row1, sizeof(cur_row1));
              status_need = !s_reading_only_page_status_valid[s_render_page] ||
                                 s_reading_only_page_status_lamps[s_render_page] != cur_status ||
                                 strcmp(s_reading_only_page_row1[s_render_page], cur_row1) != 0 ||
                                 s_reading_only_page_shift[s_render_page] != s_frame.shift_active ||
                                 s_reading_only_page_rear[s_render_page] != s_frame.rear_active ||
                                 strcmp(s_reading_only_page_temperature[s_render_page], s_frame.temperature) != 0 ||
                                 strcmp(s_reading_only_page_uptime[s_render_page], s_frame.uptime) != 0;
                bool info_need = s_trig_dot_pending ||
                                !s_reading_only_page_info_valid[s_render_page] ||
                                strcmp(s_reading_only_page_function[s_render_page], s_frame.function) != 0 ||
                                strcmp(s_reading_only_page_impedance[s_render_page], s_frame.impedance) != 0 ||
                                strcmp(s_reading_only_page_range[s_render_page], s_frame.range) != 0 ||
                                 strcmp(s_reading_only_page_rate[s_render_page], s_frame.rate) != 0 ||
                                 s_reading_only_page_info_lamps[s_render_page] != row2_info_lamps();
                if (!s_reading_only_page_info_valid[s_render_page] ||
                    strcmp(s_reading_only_page_function[s_render_page],
                           s_frame.function) != 0)
                    s_dbg_function_change_window++;
               /* Defer INFO while the axis doesn't exist yet: INFO runs
               * before TREND recomputes the axis in the pipeline, so the
               * first post-rotation pass would bake "AUTO --" into pixels
               * and caches (one flash + one wasted full repaint). The rows
               * stay complete-but-old for one composition, then paint real
               * values with the atomic flip. Boot is unaffected (no
               * transaction there); a truly dataless axis still shows an
               * honest "--" once the background completes. */
              if (!s_trend_axis_valid && s_trend_rebuild_transaction)
                  info_need = false;
               /* A unit change invalidates the trend scene before the new
                * status fields have necessarily arrived. Keep the last
                * committed row on the hidden page while the trend rebuilds;
                * painting the intermediate row here is what exposed the
                * MANUAL/blank/AUTO sequence. The completed trend stage will
                * re-enter STATUS on this same page before presenting it. */
               if (s_trend_rebuild_transaction || s_initial_page_pending ||
                   !s_display_enabled)
               {
                   if (s_trend_rebuild_transaction)
                       s_deferred_row_pending = true;
                   s_reading_only_stage = READING_ONLY_CLEAR;
               }
               else if (status_need) s_reading_only_stage = READING_ONLY_STATUS;
               else if (info_need) s_reading_only_stage = READING_ONLY_INFO;
               else s_reading_only_stage = READING_ONLY_CLEAR;
        }
        s_perf_reading_frames_window++;
        return;
    }

    switch (s_reading_only_stage)
    {
    case READING_ONLY_CLEAR:
    {
        bool page_unit_changed;
        bool page_suffix_changed;
        lt7680_status_t st;

        page_unit_changed = strcmp(s_reading_only_page_unit[s_render_page],
                                   s_frame.unit) != 0;
        page_suffix_changed = strcmp(s_reading_only_page_suffix[s_render_page],
                                     s_frame.unit_suffix) != 0;
        st = lt7680_gfx_select_canvas_page(s_render_page);
        if (st != LT7680_OK ||
            (st == LT7680_OK &&
              ((READING_ONLY_CLEAR_BAND &&
                /* Start below the INFO divider (y50 h2): the divider is
                 * painted by the INFO stage but lives inside the reading
                 * band's first two rows, so a full-band clear erases it
                 * every composition and it blinks whenever INFO skips (=
                 * almost always). Digits start at READING_VALUE_Y below. */
                ui_fill_rect(0u, (uint16_t)(MAIN_DISPLAY_READING_Y +
                                              MAIN_DISPLAY_YELLOW_LINE_H),
                             MAIN_DISPLAY_UI_WIDTH,
                             (uint16_t)(MAIN_DISPLAY_READING_H -
                                        MAIN_DISPLAY_YELLOW_LINE_H),
                             MAIN_DISPLAY_COLOR_BG) != LT7680_OK) ||
              (page_unit_changed &&
               s_reading_only_page_unit_w[s_render_page] != 0u &&
               ui_fill_rect(s_reading_only_page_unit_x[s_render_page],
                            MAIN_DISPLAY_READING_VALUE_Y,
                            s_reading_only_page_unit_w[s_render_page],
                            FONT_DIGIT_HEIGHT,
                            MAIN_DISPLAY_COLOR_BG) != LT7680_OK) ||
              (page_suffix_changed &&
               s_reading_only_page_suffix[s_render_page][0] != '\0' &&
               ui_fill_rect(s_reading_only_page_suffix_x[s_render_page],
                            MAIN_DISPLAY_DCAC_Y, FONT_HALF_WIDTH * 2u,
                            FONT_HALF_HEIGHT, MAIN_DISPLAY_COLOR_BG) !=
                   LT7680_OK))))
        {
            if (st == LT7680_OK)
                st = s_reading_only_last_error;
            if (st == LT7680_OK)
                st = LT7680_ERR_BUS;
            reading_only_abort_frame(st);
            return;
        }
        if (s_reading_only_io_error)
        {
            reading_only_abort_frame(s_reading_only_last_error);
            return;
        }
        s_reading_only_stage = READING_ONLY_VALUE;
        return;
    case READING_ONLY_VALUE:
    {
        uint8_t value_len = s_frame.no_data ? 0u : s_frame.value_len;
        if (s_reading_only_value_index < value_len)
        {
            uint8_t index = s_reading_only_value_index;
            char glyph[2] = {s_frame.value[index], '\0'};
            bool same = s_reading_only_page_value_x[s_render_page] ==
                            s_frame.start_x &&
                        s_reading_only_page_value_color[s_render_page] ==
                            s_frame.value_color &&
                        s_reading_only_page_value[s_render_page][index] ==
                            glyph[0];
            if (!same && !ui_draw_digits(
                              (uint16_t)(s_frame.start_x +
                                         (uint16_t)index * FONT_DIGIT_WIDTH),
                              s_frame.reading_y, glyph, s_frame.value_color))
            {
                if (s_reading_only_io_error)
                    reading_only_abort_frame(s_reading_only_last_error);
                return;
            }
            s_reading_only_page_value[s_render_page][index] = glyph[0];
            s_reading_only_value_index++;
            return;
        }
        if (s_reading_only_page_value[s_render_page][value_len] != '\0')
        {
            uint8_t i;
            for (i = value_len;
                 s_reading_only_page_value[s_render_page][i] != '\0'; i++)
                if (ui_fill_rect(
                    (uint16_t)(s_reading_only_page_value_x[s_render_page] +
                               (uint16_t)i * FONT_DIGIT_WIDTH),
                    MAIN_DISPLAY_READING_VALUE_Y, FONT_DIGIT_WIDTH,
                    FONT_DIGIT_HEIGHT, MAIN_DISPLAY_COLOR_BG) != LT7680_OK)
                {
                    reading_only_abort_frame(s_reading_only_last_error);
                    return;
                }
            s_reading_only_page_value[s_render_page][value_len] = '\0';
        }
        s_reading_only_page_value_x[s_render_page] = s_frame.start_x;
        s_reading_only_page_value_color[s_render_page] = s_frame.value_color;
        s_reading_only_page_value[s_render_page][value_len] = '\0';
    }
        s_reading_only_stage = READING_ONLY_UNIT;
        return;
    case READING_ONLY_UNIT:
        if (!s_frame.no_data &&
            !ui_draw_digits(s_frame.end_x, s_frame.reading_y, s_frame.unit,
                            s_frame.value_color))
        {
            if (s_reading_only_io_error)
                reading_only_abort_frame(s_reading_only_last_error);
            return;
        }
        s_reading_only_stage = READING_ONLY_SUFFIX;
        return;
    case READING_ONLY_SUFFIX:
    {
        uint16_t suffix_x = (uint16_t)(s_frame.end_x +
                                       (uint16_t)s_frame.unit_len *
                                           FONT_DIGIT_WIDTH);
        bool suffix_same = s_frame.unit_suffix[0] == '\0'
                               ? s_reading_only_page_suffix[s_render_page][0] == '\0'
                               : strcmp(s_reading_only_page_suffix[s_render_page],
                                        s_frame.unit_suffix) == 0 &&
                                 s_reading_only_page_suffix_x[s_render_page] ==
                                     suffix_x &&
                                 s_reading_only_page_suffix_color[s_render_page] ==
                                     s_frame.value_color;
        if (!s_frame.no_data && s_frame.unit_suffix[0] != '\0' &&
            !suffix_same &&
            !ui_draw_half(suffix_x, MAIN_DISPLAY_DCAC_Y, s_frame.unit_suffix,
                          s_frame.value_color))
        {
            if (s_reading_only_io_error)
                reading_only_abort_frame(s_reading_only_last_error);
            return;
        }
        s_reading_only_page_unit_x[s_render_page] = s_frame.end_x;
        s_reading_only_page_unit_w[s_render_page] = (uint16_t)(
            (uint16_t)s_frame.unit_len * FONT_DIGIT_WIDTH +
            (s_frame.unit_suffix[0] != '\0' ? FONT_HALF_WIDTH * 2u : 0u));
        strncpy(s_reading_only_page_unit[s_render_page], s_frame.unit,
                sizeof(s_reading_only_page_unit[0]) - 1u);
        s_reading_only_page_unit[s_render_page]
            [sizeof(s_reading_only_page_unit[0]) - 1u] = '\0';
        strncpy(s_reading_only_page_suffix[s_render_page],
                s_frame.unit_suffix,
                sizeof(s_reading_only_page_suffix[0]) - 1u);
        s_reading_only_page_suffix[s_render_page]
            [sizeof(s_reading_only_page_suffix[0]) - 1u] = '\0';
        s_reading_only_page_suffix_x[s_render_page] =
            suffix_x;
        s_reading_only_page_suffix_color[s_render_page] =
            s_frame.value_color;
        s_reading_only_trend_column = 0u;
        s_reading_only_trend_bg_failed = false;
        {
            const char *trend_unit = trend_buffer_display_unit(&s_trend);
            uint32_t now = HAL_GetTick();
            bool trend_backgrounds_ready =
                s_reading_only_page_trend_bg_valid[0] &&
                s_reading_only_page_trend_bg_valid[1] &&
                strcmp(s_reading_only_page_trend_unit[0], trend_unit) == 0 &&
                strcmp(s_reading_only_page_trend_unit[1], trend_unit) == 0;

            if (trend_backgrounds_ready && s_trend_scroll_ms != 0u &&
                (uint32_t)(now - s_trend_scroll_ms) < 100u)
                s_reading_only_stage = READING_ONLY_PRESENT;
            else
                s_reading_only_stage = READING_ONLY_TREND;
        }
        return;
    }
    case READING_ONLY_TREND:
    {
        uint32_t now = HAL_GetTick();
        const char *trend_unit = trend_buffer_display_unit(&s_trend);
        bool background_ready;

        /* Stale-composition kill: a rotation sample that lands mid-pipeline
         * leaves s_frame on the old unit while the trend buffer already
         * carries the new one. Finishing that composition presents a mixed
         * page (old rows/reading + new background), and the next fresh
         * composition presents the new rows again — the visible "second
         * row paints twice, axis/top update before the reading" sequence.
         * Abandon back to IDLE for a fresh reformat instead; the partial
         * hidden work is opaque and gets fully repainted. Bounded cost:
         * IDLE re-runs within the 33 ms throttle (worst case one extra
         * partial pipeline, ~50 ms, vs seconds of mixed display).
         * Stem-to-stem compare: the frame unit has the DC/AC half-height
         * suffix split off ("VDC"->"V") while the buffer keeps the full
         * unit — a raw strcmp is true for every VDC-family frame and would
         * abandon forever (zero commits, silent serial). */
    {
        char trend_stem[TREND_UNIT_ID_MAX];
            size_t stem_n;
            strncpy(trend_stem, trend_unit, sizeof(trend_stem) - 1u);
            trend_stem[sizeof(trend_stem) - 1u] = '\0';
            stem_n = strlen(trend_stem);
            if (stem_n >= 3u &&
                ((trend_stem[stem_n - 2u] == 'D' &&
                  trend_stem[stem_n - 1u] == 'C') ||
                 (trend_stem[stem_n - 2u] == 'A' &&
                  trend_stem[stem_n - 1u] == 'C')))
                trend_stem[stem_n - 2u] = '\0';
            /* The trend buffer may advance while this cooperative frame is
             * being painted. Do not discard completed reading work and replay
             * STATUS/INFO/VALUE here: that was the source of the long gear
             * change stall. The frame snapshot owns the reading; the next
             * 33 ms snapshot will pick up the newer trend identity. */
        }

        if (s_trend_axis_valid && strcmp(s_trend_axis_unit, trend_unit) != 0)
        {
            s_trend_axis_valid = false;
            reading_only_invalidate_trend_pages();
        }
        main_display_format_trend(&s_trend, now, s_frame.unit, &s_frame);
        /* Stat decimals follow the reading ("4.0000Ω" next to "2.3624Ω",
         * not span-rounded "4.0Ω"). No '.' in value (OVERFLOW/empty) keeps
         * the span-based texts. */
        {
            const char *dot = strchr(s_frame.value, '.');
            if (s_frame.trend_has_data && dot != 0)
            {
                uint8_t dec = 0u;
                const char *p = dot + 1u;
                float scale = trend_buffer_display_scale(&s_trend);
                const char *au = trend_buffer_display_unit(&s_trend);
                while (*p >= '0' && *p <= '9' && dec < 5u) { dec++; p++; }
                main_display_format_stat_fixed(
                    s_frame.trend_stat_maximum * scale, au, dec,
                    s_frame.trend_stat_maximum_text,
                    sizeof(s_frame.trend_stat_maximum_text));
                main_display_format_stat_fixed(
                    s_frame.trend_stat_minimum * scale, au, dec,
                    s_frame.trend_stat_minimum_text,
                    sizeof(s_frame.trend_stat_minimum_text));
                main_display_format_stat_fixed(
                    s_frame.trend_stat_average * scale, au, dec,
                    s_frame.trend_stat_average_text,
                    sizeof(s_frame.trend_stat_average_text));
            }
        }
        if (s_frame.trend_has_data && s_trend_axis_valid &&
            s_frame.trend_minimum >= s_trend_axis_min &&
            s_frame.trend_maximum <= s_trend_axis_max)
        {
            s_frame.trend_minimum = s_trend_axis_min;
            s_frame.trend_maximum = s_trend_axis_max;
            main_display_format_linear_trend_labels(&s_frame);
        }
        else if (s_frame.trend_has_data)
        {
            bool axis_expanded = false;
            float axis_min;
            float axis_max;
            float span;

            /* main_display_format_trend() has already selected the smallest
             * standard Keithley range that contains the 10 s peak. Keep that
             * fixed axis here; never replace it with demo-only bounds. */
            if (!s_trend_axis_valid)
            {
                float peak = s_frame.trend_maximum > -s_frame.trend_minimum
                                 ? s_frame.trend_maximum
                                 : -s_frame.trend_minimum;

                keithley_trend_axis_range(trend_unit, peak, &axis_min,
                                          &axis_max);
                axis_expanded = true;
            }
            else
            {
                /* Fixed range — never expand on live-data clip. */
                axis_expanded = false;
                axis_min = s_trend_axis_min;
                axis_max = s_trend_axis_max;
            }
            span = axis_max - axis_min;
            if (span < 0.000001f)
                span = 0.000001f;
            s_trend_axis_min = axis_min;
            s_trend_axis_max = axis_max;
            s_trend_axis_valid = true;
            s_frame.trend_minimum = s_trend_axis_min;
            s_frame.trend_maximum = s_trend_axis_max;
            strncpy(s_trend_axis_unit, trend_unit, TREND_UNIT_ID_MAX - 1u);
            s_trend_axis_unit[TREND_UNIT_ID_MAX - 1u] = '\0';
            /* ADR-0007: left gutter shows max/mid/min of the resident span. */
            main_display_format_linear_trend_labels(&s_frame);
            if (axis_expanded)
            {
                /* Clean background; the sweep's own axis-move detector
                 * restarts a live-window rescan at the new scale (no
                 * per-slot cursor reset needed here). */
                reading_only_invalidate_trend_pages();
            }
        }
        background_ready = s_reading_only_page_trend_bg_valid[s_render_page] &&
                           strcmp(s_reading_only_page_trend_unit[s_render_page],
                                  trend_buffer_display_unit(&s_trend)) == 0;
        bool background_was_ready = background_ready;
        if (!reading_only_render_trend_background())
        {
            if (s_reading_only_io_error)
            {
                s_reading_only_page_trend_bg_valid[s_render_page] = false;
                s_reading_only_trend_bg_failed = true;
                s_reading_only_io_error = false;
                s_reading_only_stage = READING_ONLY_PRESENT;
            }
            return;
        }
        if (!background_was_ready)
        {
            /* Fresh background (unit change or axis rescale): the sweep
             * re-anchors through the trend-buffer reset detector or the
             * rescale restart above and redraws the window within its
             * per-pass budget. */
            s_reading_only_page_trend_curve_valid[s_render_page] = true;
            s_trend_rebuild_transaction = false;
            /* The row was deliberately deferred while this transaction was
             * rebuilding. Repaint it before the first commit of the new
             * unit, so the visible page never contains mixed generations.
             * The snapshot is deliberately NOT cleared here: the deferred
             * paint and the following drain must use the same frozen rows,
             * otherwise churn lands between the two pages' paints and they
             * alternate visibly on slow flips. Quiet IDLE clears it. */
            if (s_deferred_row_pending)
            {
                /* VALUE/UNIT/SUFFIX already completed on this page. The row
                 * is the only stale band left, so skip the second reading
                 * pass and continue directly with the atomic row update. */
                s_reading_only_stage = READING_ONLY_STATUS;
                return;
            }
            /* Do not synchronously clone the whole UI page here. The former
             * eight-band loop blocked the scheduler for 40+ ms and exposed
             * the row as a separate visual generation. The next hidden-page
             * pass synchronizes the changed regions with the normal bounded
             * renderer. */
            #if 0
            {
                /* Full-page BTE exceeds wait_bte_idle (~10-20 ms), so clone
                 * in 8 horizontal bands (~5 ms each). All-or-nothing: caches
                 * mirror only when every band lands. */
                uint8_t sib = (uint8_t)(s_render_page ^ 1u);
                lt7680_rect_t full;
                lt7680_status_t cs = LT7680_ERR_PARAM;
                uint8_t band;
                panel_transform_ui_rect_to_fb(0u, 0u, MAIN_DISPLAY_UI_WIDTH,
                                              MAIN_DISPLAY_UI_HEIGHT,
                                              &full.x, &full.y,
                                              &full.w, &full.h);
                if (full.w != 0u && full.h != 0u)
                {
                    cs = LT7680_OK;
                    for (band = 0u; band < 8u && cs == LT7680_OK; band++)
                    {
                        lt7680_rect_t strip = full;
                        strip.y = (uint16_t)(full.y + band * (full.h / 8u));
                        strip.h = (uint16_t)(band < 7u ? full.h / 8u
                                                       : full.h - 7u * (full.h / 8u));
                        cs = lt7680_gfx_copy_rect(s_render_page, sib, &strip);
                    }
                    if (cs == LT7680_OK)
                    {
                        s_reading_only_page_status_valid[sib] =
                            s_reading_only_page_status_valid[s_render_page];
                        s_reading_only_page_status_lamps[sib] =
                            s_reading_only_page_status_lamps[s_render_page];
                    memcpy(s_reading_only_page_row1[sib],
                           s_reading_only_page_row1[s_render_page],
                           sizeof(s_reading_only_page_row1[0]));
                    memcpy(s_reading_only_page_active_status[sib],
                           s_reading_only_page_active_status[s_render_page],
                           sizeof(s_reading_only_page_active_status[0]));
                    memcpy(s_reading_only_page_temperature[sib],
                           s_reading_only_page_temperature[s_render_page],
                           sizeof(s_reading_only_page_temperature[0]));
                    memcpy(s_reading_only_page_uptime[sib],
                           s_reading_only_page_uptime[s_render_page],
                           sizeof(s_reading_only_page_uptime[0]));
                    memcpy(s_reading_only_page_brand[sib],
                           s_reading_only_page_brand[s_render_page],
                           sizeof(s_reading_only_page_brand[0]));
                    memcpy(s_reading_only_page_function[sib],
                           s_reading_only_page_function[s_render_page],
                           sizeof(s_reading_only_page_function[0]));
                    memcpy(s_reading_only_page_impedance[sib],
                           s_reading_only_page_impedance[s_render_page],
                           sizeof(s_reading_only_page_impedance[0]));
                    memcpy(s_reading_only_page_range[sib],
                           s_reading_only_page_range[s_render_page],
                           sizeof(s_reading_only_page_range[0]));
                    memcpy(s_reading_only_page_rate[sib],
                           s_reading_only_page_rate[s_render_page],
                           sizeof(s_reading_only_page_rate[0]));
                    s_reading_only_page_info_lamps[sib] =
                        s_reading_only_page_info_lamps[s_render_page];
                    s_reading_only_page_trig_dot[sib] =
                        s_reading_only_page_trig_dot[s_render_page];
                    s_reading_only_page_info_valid[sib] =
                        s_reading_only_page_info_valid[s_render_page];
                    memcpy(s_reading_only_page_trend_unit[sib],
                           s_reading_only_page_trend_unit[s_render_page],
                           sizeof(s_reading_only_page_trend_unit[0]));
                    s_reading_only_page_trend_bg_valid[sib] = true;
                    s_reading_only_page_trend_curve_valid[sib] = true;
                    memcpy(s_drawn_trend_y0[sib],
                           s_drawn_trend_y0[s_render_page],
                           sizeof(s_drawn_trend_y0[0]));
                    memcpy(s_drawn_trend_y1[sib],
                           s_drawn_trend_y1[s_render_page],
                           sizeof(s_drawn_trend_y1[0]));
                    memcpy(s_drawn_trend_occupied[sib],
                           s_drawn_trend_occupied[s_render_page],
                           sizeof(s_drawn_trend_occupied[0]));
                    s_trend_grid_dirty[sib] =
                        s_trend_grid_dirty[s_render_page];
                    }
                }
            }
            #endif
            s_reading_only_stage = READING_ONLY_STATUS;
            return;
        }
        /* ADR-0006: plot-only band — the sweep dual-page-writes every pixel,
         * so no region flag is needed before the flip. */
        if (!trend_sweep_advance())
            s_reading_only_io_error = false;
        /* Live stat refresh: one bounded slice per frame, then ALWAYS
         * present — the pass resumes next frame. Blocking present on pass
         * completion froze the reading for the whole pass whenever stats
         * churned (stall-then-recover). Dual-page + per-slot snapshot
         * publish keep every cut point flicker-free. Skip while the sweep
         * is draining a post-rotation backlog: the background snapshot is
         * already fresh, and a wholesale live repaint on top of the drain
         * just doubles the storm. The due flag stays set, so live resumes
         * on its own once the cursor catches up (no starvation: drain
         * outruns arrivals). */
        {
            uint32_t sweep_behind =
                (s_trend.has_sample &&
                 s_trend.newest_bucket > s_sweep_cursor_bucket)
                    ? s_trend.newest_bucket - s_sweep_cursor_bucket
                    : 0u;
            if (sweep_behind <= 32u && trend_live_due(now))
                (void)reading_only_render_trend_live();
        }
        s_reading_only_page_trend_curve_valid[s_render_page] = true;
        s_reading_only_stage = READING_ONLY_PRESENT;
        return;
    }
    case READING_ONLY_PRESENT:
    {
        if (s_trend_rebuild_transaction)
        {
            s_dbg_present_hold_window++;
            /* A range/unit change is a visual transaction: keep the old
             * visible page until the hidden page contains the complete
             * trend chrome, axis and plot. Presenting each scheduler slice
             * exposes the left-to-right/top-to-bottom rebuild. */
            s_reading_only_stage = READING_ONLY_TREND;
            return;
        }
        lt7680_status_t st = lt7680_gfx_present_page(s_render_page);
        if (st == LT7680_OK)
            st = lt7680_write_reg(0x12u, 0x48u);
        if (st != LT7680_OK)
        {
            reading_only_abort_frame(st);
            return;
        }
        s_display_enabled = true;
        s_visible_page = s_render_page;
        perf_note_present();
        s_reading_only_dirty =
            s_reading_only_generation != s_reading_only_frame_generation;
        s_frame_rendering = false;
        s_renderer.phase = RENDER_PHASE_IDLE;
        s_perf_display_commits_window++;
        perf_record_frame();
        s_reading_only_stage = READING_ONLY_IDLE;
        return;
    }
    default:
        s_frame_rendering = false;
        s_reading_only_stage = READING_ONLY_IDLE;
        return;
    }
    }
}
#endif

static void reading_scene_render(void)
{
#if K2000_READING_ONLY_BASELINE
    reading_only_render();
    return;
#else
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
     * the 5 Hz graph. While a trend pass runs, trend_wire_pending_regions()
     * raises arriving region dirties in the scheduler and refreshes the text
     * snapshot, so the next slice boundary (<8 columns or one axis
     * background/grid/label operation) preempts toward the newest values;
     * between boundaries snapshots wait coalesced -- never dropped -- and the
     * active pass is never restarted (an interrupted axes phase replays its
     * deterministic sequence; columns resume at their own cursor). */
    if (s_renderer.phase == RENDER_PHASE_IDLE)
    {
        bool status_dirty =
            (s_ui_dirty_regions & RENDER_DIRTY_STATUS) != 0u;
        due_regions = (uint8_t)(s_ui_dirty_regions & RENDER_DIRTY_READING);
        /* Reading render throttle: above ~30 readings/s the intermediate
         * values can never be shown (commits are bounded by
         * DISPLAY_FRAME_PERIOD_MS), yet each one costs a full digit diff +
         * BTE blit pass that delays the commit that WOULD have shown the
         * newest value. Keep the dirty bit set -- the next turn past the
         * throttle window renders whatever the newest value is then. */
        if ((due_regions & RENDER_DIRTY_READING) != 0u &&
            (now - s_text_refresh_tick) < DISPLAY_FRAME_PERIOD_MS)
        {
            due_regions &= (uint8_t)~RENDER_DIRTY_READING;
        }
        trend_due = (now - s_trend_refresh_tick) >= 500u;
        display_due = (uint32_t)(now - s_display_due_tick) >=
                      DISPLAY_FRAME_PERIOD_MS;
        if (display_due && (due_regions != 0u || status_dirty ||
                            trend_due || s_perf_sample_count != 0u))
        {
            s_render_page = (uint8_t)(s_visible_page ^ 1u);
            main_display_format(&s_ui, &s_frame);
            perf_format_display(s_frame.gpib);
            /* Content-change gate: a STATUS flag alone (set by every
             * sample's status TAGs) must not trigger the expensive
             * top-bar + info-cell repaint unless something rendered in
             * them actually changed. */
            if (status_dirty)
            {
                s_ui_dirty_regions &= (uint8_t)~RENDER_DIRTY_STATUS;
                if (status_view_changed())
                    due_regions |= RENDER_DIRTY_STATUS;
            }
            (void)trend_buffer_project(&s_trend, now, s_trend_columns,
                                       TREND_MAX_COLUMNS);
             /* Auto-fit first: the frame carries the fresh 1/2/5 identity
              * plus the raw window data bounds. The shared trend-axis
              * module then decides which axis this frame renders with. */
             main_display_format_trend(&s_trend, now, s_frame.unit,
                                       &s_frame);
             {
                 /* Shared pure decision (trend_axis.c): resident/candidate
                  * acceptance rules including the filling-window freeze.
                  * Regression-tested against this exact call shape. */
                 trend_axis_verdict_t verdict = trend_axis_update(
                     &s_trend_axis_resident, &s_trend_axis_candidate,
                     &s_frame, trend_buffer_display_scale(&s_trend),
                     trend_buffer_window_full(&s_trend), now);
                 if (verdict.keep_resident)
                 {
                     /* Project columns onto the RESIDENT axis: overwrite the
                      * frame's display bounds with its span. trend_draw_column()
                      * clamps fy0/fy1 to 0..MAIN_DISPLAY_PLOT_H-1 before the
                      * uint8_t cast, so out-of-range data saturates at the
                      * plot edges instead of forcing a rebuild. */
                     float scale = trend_buffer_display_scale(&s_trend);
                     main_display_set_trend_axis(
                         &s_frame, s_trend_axis_resident.step,
                         s_trend_axis_resident.top,
                         s_trend_axis_resident.unit);
                     s_frame.trend_minimum =
                         (s_trend_axis_resident.top -
                          3.0f * s_trend_axis_resident.step) / scale;
                     s_frame.trend_maximum =
                         s_trend_axis_resident.top / scale;
                 }
               /* Telemetry semantics: trend_axis_rebuilds counts every
                * snapshot that scheduled a FULL trend repaint pass, not
                * only unit-change axis rebuilds -- the initial page fill
                * and a per-page has-data mismatch are counted too, and one
                * pass is counted once here (at decision time), regardless
                * of how many scheduler slices it later spans. Same-unit
                * drift within the resident axis must NOT increment. */
              s_trend_relabel_only =
                  !s_initial_page_pending &&
                  s_page_trend_has_data[s_visible_page] ==
                      s_frame.trend_has_data &&
                  s_frame.trend_has_data &&
                  strcmp(s_trend_axis_resident.unit,
                         s_frame.trend_axis_unit) == 0;
               s_trend_full_repaint = s_initial_page_pending ||
                                    /* The non-visible page receives the current visible trend
                                     * band before incremental columns are drawn, so compare this
                                     * snapshot with the visible-page scale. Comparing its stale
                                     * pre-copy cache would force a needless full trend rebuild. */
                                    s_page_trend_has_data[s_visible_page] !=
                                        s_frame.trend_has_data ||
                                      verdict.axis_rebuild;
              if (s_trend_full_repaint)
                  s_perf_axis_rebuilds_window++;
              }
              /* Input-path test builds drop trend requests entirely so the
               * reading pipeline can be measured without graph turns. */
              trend_needed = !K2000_TREND_TEST_DISABLE &&
                             (trend_due || s_trend_full_repaint);
              if (begin_hidden_frame())
            {
                if ((due_regions & RENDER_DIRTY_READING) != 0u)
                {
                    s_text_generation++;
                    s_perf_reading_frames_window++;
                }
                s_frame_text_generation = s_text_generation;
                 page_text_stale = due_regions != 0u;
                 if ((due_regions & RENDER_DIRTY_READING) != 0u)
                     s_text_refresh_tick = now;
                 if (trend_due)
                     s_trend_refresh_tick = now;
                 s_display_due_tick = now;
                 s_render_status_regions =
                     (due_regions & RENDER_DIRTY_STATUS) != 0u;
                 if (page_text_stale)
                     render_scheduler_request_regions(&s_renderer, due_regions);
                 if (trend_needed)
                 {
                     render_scheduler_request_trend(&s_renderer);
                     s_frame_has_trend_update = true;
                 }
                 /* Trend requests only raise the low-priority flag; start
                  * whichever work is queued now (regions first). Without
                  * this a trend-only frame would wait for the next turn. */
                  render_scheduler_kick(&s_renderer);
                  s_ui_dirty_regions &= (uint8_t)~due_regions;
            }
        }
    }
    else if (s_frame_rendering &&
             (s_renderer.phase == RENDER_PHASE_UPDATE_TREND_AXES ||
              s_renderer.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS))
    {
        /* Active graph pass: give newly arrived region work its priority
         * slot at the next bounded slice boundary (review I-1). */
        trend_wire_pending_regions();
    }
    initial_phase = s_renderer.phase <= RENDER_PHASE_INITIAL_TREND_COLUMNS;
    /* Record which band this frame is mutating. The flag survives until the
     * NEXT begin_hidden_frame() replays it onto the sibling page, so a band
     * written while this page was hidden can never be lost at the flip. */
    if (s_frame_rendering)
        s_frame_regions |= frame_region_for_phase(s_renderer.phase);
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
            /* Reading-band sync extent starts empty each frame and grows
             * to cover exactly what this frame will touch. */
            s_reading_sync_x0 = (uint16_t)MAIN_DISPLAY_UI_WIDTH;
            s_reading_sync_x1 = 0u;
#if RIF_BTE_RENDERER
            /* Plan the frame's digit cells and keep every cell whose glyph
             * identity is unchanged; only changed/vanished cells touch the
             * hardware. Any state drift falls back to the full band clear. */
            bool stable =
                s_renderer.phase == RENDER_PHASE_UPDATE_READING &&
                s_rif_ready && s_rif_dma_probe_passed && !s_frame.no_data &&
                s_frame.value_color == MAIN_DISPLAY_COLOR_GREEN &&
                s_prev_reading_color == s_frame.value_color &&
                s_prev_reading_nodata == (uint8_t)s_frame.no_data;
            s_prev_reading_color = s_frame.value_color;
            s_prev_reading_nodata = (uint8_t)s_frame.no_data;

            /* Remember the previous frame's suffix footprint so the tail
             * erase below can cover it. No band fill here: a mid-band erase
             * punches through to the visible page before the replacement
             * glyphs blit later in the same frame. */
            if (s_frame.unit_suffix[0] == '\0')
            {
                s_prev_suffix[0] = '\0';
                s_prev_suffix_x = 0u;
                s_prev_suffix_color = 0u;
            }

            {
                static rif_cell_t planned[RIF_CELL_MAX];
                uint8_t planned_count = 0u;
                const char *p;
                uint16_t cx;

                p = s_frame.value;
                cx = s_frame.start_x;
                while (*p != '\0' && planned_count < RIF_CELL_MAX)
                {
                    uint32_t kind;
                    uint16_t code;
                    uint8_t advance;
                    if (!rif_text_code(p, &kind, &code, &advance))
                        break;
                    planned[planned_count].x = cx;
                    planned[planned_count].y = s_frame.reading_y;
                    planned[planned_count].kind = kind;
                    planned[planned_count].code = code;
                    planned[planned_count].fresh = 1u;
                    planned_count++;
                    cx = (uint16_t)(cx + FONT_DIGIT_WIDTH);
                    p += advance;
                }
                p = s_frame.unit;
                cx = s_frame.end_x;
                while (*p != '\0' && planned_count < RIF_CELL_MAX)
                {
                    uint32_t kind;
                    uint16_t code;
                    uint8_t advance;
                    if (!rif_text_code(p, &kind, &code, &advance))
                        break;
                    planned[planned_count].x = cx;
                    planned[planned_count].y = s_frame.reading_y;
                    planned[planned_count].kind = kind;
                    planned[planned_count].code = code;
                    planned[planned_count].fresh = 1u;
                    planned_count++;
                    cx = (uint16_t)(cx + FONT_DIGIT_WIDTH);
                    p += advance;
                }

                if (stable)
                {
                    /* Keep identical cells (skip their blit). Vanished cells
                     * always form one contiguous tail (left-aligned text),
                     * so erase it as a single rect that starts at the new
                     * content edge -- never on top of a glyph that this
                     * frame will redraw. */
                    uint8_t i;
                    uint16_t new_extent = s_frame.start_x;
                    uint16_t old_extent = s_frame.start_x;
                    for (i = 0u; i < planned_count; i++)
                    {
                        uint16_t e = (uint16_t)(planned[i].x +
                                                FONT_DIGIT_WIDTH);
                        if (e > new_extent)
                            new_extent = e;
                    }
                    for (i = 0u; i < s_cell_count; i++)
                    {
                        uint16_t e = (uint16_t)(s_cells[i].x +
                                                FONT_DIGIT_WIDTH);
                        if (e > old_extent)
                            old_extent = e;
                    }
                    if (s_prev_suffix[0] != '\0')
                    {
                        uint16_t e = (uint16_t)(s_prev_suffix_x +
                                                FONT_HALF_WIDTH * 2u);
                        if (e > old_extent)
                            old_extent = e;
                    }
                    if (old_extent > new_extent)
                    {
                        (void)ui_fill_rect(
                            new_extent, MAIN_DISPLAY_READING_VALUE_Y,
                            (uint16_t)(old_extent - new_extent),
                            FONT_DIGIT_HEIGHT, MAIN_DISPLAY_COLOR_BG);
                    }
                    /* Mark planned cells that already sit on canvas with the
                     * same glyph identity: their blit can be skipped. */
                    for (i = 0u; i < planned_count; i++)
                    {
                        uint8_t j;
                        for (j = 0u; j < s_cell_count; j++)
                        {
                            if (s_cells[j].x == planned[i].x &&
                                s_cells[j].y == planned[i].y &&
                                s_cells[j].kind == planned[i].kind &&
                                s_cells[j].code == planned[i].code)
                            {
                                planned[i].fresh = 0u;
                                break;
                            }
                        }
                    }
                    {
                        uint16_t x_end = new_extent > old_extent
                                             ? new_extent
                                             : old_extent;

                        if (s_frame.unit_suffix[0] != '\0')
                        {
                            uint16_t se =
                                (uint16_t)(s_frame.end_x +
                                           (uint16_t)s_frame.unit_len *
                                               FONT_DIGIT_WIDTH +
                                           FONT_HALF_WIDTH * 2u);
                            if (se > x_end)
                                x_end = se;
                        }
                        if (s_frame.start_x < s_reading_sync_x0)
                            s_reading_sync_x0 = s_frame.start_x;
                        if (x_end > s_reading_sync_x1)
                            s_reading_sync_x1 = x_end;
                    }
                    s_cell_count = 0u;
                    for (i = 0u; i < planned_count; i++)
                        s_cells[s_cell_count++] = planned[i];
                    s_reading_diff = true;
                }
                else
                {
                    s_cell_count = 0u;
                    for (uint8_t i = 0u; i < planned_count; i++)
                        s_cells[s_cell_count++] = planned[i];
                    s_reading_diff = false;
                    s_reading_sync_x0 = 0u;
                    s_reading_sync_x1 = (uint16_t)MAIN_DISPLAY_UI_WIDTH;
                    (void)ui_fill_rect(0u, MAIN_DISPLAY_READING_Y, 960u,
                                       MAIN_DISPLAY_READING_H,
                                       MAIN_DISPLAY_COLOR_BG);
                }
            }
#else
            s_reading_sync_x0 = 0u;
            s_reading_sync_x1 = (uint16_t)MAIN_DISPLAY_UI_WIDTH;
            (void)ui_fill_rect(0u, MAIN_DISPLAY_READING_Y, 960u,
                               MAIN_DISPLAY_READING_H, MAIN_DISPLAY_COLOR_BG);
#endif
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
                s_reading_sync_x0 = 0u;
                s_reading_sync_x1 = (uint16_t)MAIN_DISPLAY_UI_WIDTH;
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
                uint16_t sfx_x = (uint16_t)(
                    s_frame.end_x +
                    (uint16_t)s_frame.unit_len * FONT_DIGIT_WIDTH);
                /* Skip when the suffix already sits on canvas unchanged:
                 * redrawing it every reading frame churned dozens of GE
                 * fills through the visible page for zero visual change. */
                if (strcmp(s_frame.unit_suffix, s_prev_suffix) == 0 &&
                    sfx_x == s_prev_suffix_x &&
                    s_frame.value_color == s_prev_suffix_color)
                {
                    s_render_item++;
                    return;
                }
                if (ui_draw_half(sfx_x, MAIN_DISPLAY_DCAC_Y,
                                 s_frame.unit_suffix,
                                 s_frame.value_color))
                {
                    /* Record the footprint for the next frame's tail erase. */
                    strncpy(s_prev_suffix, s_frame.unit_suffix,
                            sizeof(s_prev_suffix) - 1u);
                    s_prev_suffix[sizeof(s_prev_suffix) - 1u] = '\0';
                    s_prev_suffix_x = sfx_x;
                    s_prev_suffix_color = s_frame.value_color;
                    s_render_item++;
                }
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
            /* Info cells live in the band's far end -- widen the sync. */
            if (MAIN_DISPLAY_INFO_X < s_reading_sync_x0)
                s_reading_sync_x0 = MAIN_DISPLAY_INFO_X;
            if (MAIN_DISPLAY_INFO_RIGHT > s_reading_sync_x1)
                s_reading_sync_x1 = MAIN_DISPLAY_INFO_RIGHT;
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
            uint16_t y = (uint16_t)(MAIN_DISPLAY_PLOT_Y + i * MAIN_DISPLAY_PLOT_H / 2u);
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
            /* Rescale-in-place: wipe only the Y-label gutter. Plot
             * surface, grid geometry and X labels are pixel-identical;
             * only the Y label text changes meaning. */
            if (!s_trend_relabel_only)
                trend_draw_background();
            else
                (void)ui_fill_rect(0u, MAIN_DISPLAY_TREND_Y,
                                   MAIN_DISPLAY_PLOT_X,
                                   MAIN_DISPLAY_TREND_H,
                                   MAIN_DISPLAY_COLOR_BAR);
            s_render_item = 1u;
            /* No yield inside AXES: a mid-sequence preempt resets the shared
             * item cursor to 0, and with resume_at_columns still false the
             * next turn replays from the background -- under sustained 10 Hz
             * readings that restart loops forever (measured livelock) while
             * redrawing the full chart background each time. The whole
             * sequence is ~19 fast GE ops (tens of ms); that bounded wait IS
             * the reading-priority contract for full rebuilds. */
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
                                    i * MAIN_DISPLAY_PLOT_H / 2u);
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
        if (!s_trend_relabel_only &&
            s_render_item < MAIN_DISPLAY_Y_LABEL_COUNT * 2u +
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
        s_trend_relabel_only = false;
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
            trend_draw_column(s_render_column, !initial_phase);
            s_perf_trend_columns_window++;
            s_render_column++;
        }
        /* Bounded slice: hand control back to a reading/status update that
         * arrived while the graph was running. The unfinished pass stays
         * flagged and resumes DIRECTLY at this columns phase -- its axes
         * phase already completed, so the rebuild can never replay the
         * background clear over columns rendered before the handoff.
         * s_render_column is untouched across the slice boundary: no
         * column is redrawn, skipped, or erased. */
        if (s_render_column < TREND_MAX_COLUMNS &&
            trend_yield_to_regions())
            return;
        if (s_render_column >= TREND_MAX_COLUMNS)
        {
            s_render_column = 0u;
            if (s_trend_grid_dirty[s_render_page])
            {
                s_trend_grid_dirty[s_render_page] = false;
                trend_restore_grid(
                    MAIN_DISPLAY_PLOT_X,
                    (uint16_t)(MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W),
                    MAIN_DISPLAY_PLOT_Y,
                    (uint16_t)(MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H));
                return;
            }
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
#endif
}

static const scene_t s_reading_scene = {
    reading_scene_enter,
    reading_scene_exit,
    reading_scene_render,
};

static void update_blink(void)
{
    uint32_t now = HAL_GetTick();

#if K2000_READING_ONLY_BASELINE
    /* TRIGGER dot blink (ADR-0004): independent 250 ms phase. On toggle while
     * TRIG is active, force a frame so the INFO stage repaints the dot; the
     * INFO fast path then costs a single 8x8 fill. */
    if ((uint32_t)(now - s_trig_dot_tick) >= 250u)
    {
        s_trig_dot_tick = now;
        s_trig_dot_phase = !s_trig_dot_phase;
        if (status_bar_active(&s_ui.status, 0x08u, 0x08u))
        {
            s_reading_only_dirty = true;
            s_trig_dot_pending = true;
        }
    }
#endif
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
    internal_temperature_init();

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
        hal_uart_send_text("\r\nK2000 TFT reading-only 500Hz baseline");
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
#if K2000_READING_ONLY_BASELINE
                            /* PIP dormant: this die never composites PIP1 or
                             * PIP2 windows (verified 2026-08-30). Trend uses
                             * the main-window renderer only. */
#endif
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
                             main_display_format_trend(&s_trend, HAL_GetTick(),
                                                       s_frame.unit,
                                                       &s_frame);
                            (void)trend_buffer_project(&s_trend, HAL_GetTick(),
                                                       s_trend_columns, TREND_MAX_COLUMNS);
                             s_frame_rendering = true;
                             s_frame_has_trend_update = true;
                             s_ui_dirty_regions = 0u;
                             s_initial_page_pending = true;
#if K2000_READING_ONLY_BASELINE
                             /* The baseline has no cooperative first-frame
                              * pipeline. Start the demo after exposing the
                              * known-black page; its first sample owns the
                              * first visible reading frame. */
                             s_initial_page_pending = false;
                             s_frame_rendering = false;
                             s_frame_has_trend_update = false;
                             s_renderer.phase = RENDER_PHASE_IDLE;
                             s_display_enabled = true;
                             s_demo_last_tick = HAL_GetTick();
                             s_demo_status_tick = HAL_GetTick();
#endif
                             hal_uart_send_text("PASS framebuffer ready, building hidden frame\r\n");
                            s_display_ready = true;
                            hal_uart_send_text("\r\nINIT-OK\r\n");
                            /* Arm DWT cycle counter + TRCENA so the
                             * SysTick PC sampler has live data. */
                            *((volatile uint32_t *)0xE000EDFCu) |= (1u << 24u);
                            *((volatile uint32_t *)0xE0001000u) |= (1u << 0u);
                            wdt_report_boot();
                            /* IWDG stays OFF: bisection proved its mere
                             * activation kills the runtime (<1 s after
                             * INIT, looping forever), while an identical
                             * build without it runs indefinitely. Root
                             * cause inside the IWDG/LSI interaction is
                             * unresolved; the [STALL]/[FAULT]/[RST]
                             * black boxes remain available without it. */
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

        {
            uint32_t now_loop = HAL_GetTick();
            uint32_t gap = now_loop - s_loop_last_tick;

            if (gap > s_loop_max_gap_ms)
                s_loop_max_gap_ms = gap;
            if (gap > 200u && !s_loop_stall_printed)
            {
                hal_uart_send_text("[STALL] gap=");
                perf_send_u32(gap);
                hal_uart_send_text(" tick=");
                perf_send_u32(now_loop);
                hal_uart_send_text(" phase=");
                hal_uart_send_hex8((uint8_t)s_renderer.phase);
                hal_uart_send_text(" item=");
                hal_uart_send_hex8(s_render_item);
                hal_uart_send_text(" col=");
                hal_uart_send_hex8((uint8_t)s_render_column);
                hal_uart_send_text("\r\n");
                s_loop_stall_printed = 1u;
                s_loop_stall_phase = (uint8_t)s_renderer.phase;
            }
            if (gap <= 200u)
                wdt_kick();
            /* APB2 clock-enable guard: one register holds SPI1, USART1,
             * AFIO and all sample GPIOs. A corrupted enable-write during
             * a heavy BTE burst (power-sag window) kills every peripheral
             * at once -- observed as simultaneous UART/SPI/GPIO death.
             * Detect, report, restore. */
            {
                const uint32_t need =
                    0x1u      /* AFIOEN */
                  | 0x4u      /* IOPAEN */
                  | 0x8u      /* IOPBEN */
                  | 0x10u     /* IOPCEN */
                  | 0x1000u   /* SPI1EN */
                  | 0x4000u;  /* USART1EN */
                /* RCC_APB2ENR is base+0x18. An earlier revision poked
                 * base+0x10 -- APB1RSTR! -- strobing reset pulses into
                 * half of APB1 on every heartbeat. */
                volatile uint32_t *apb2enr =
                    (volatile uint32_t *)0x40021018u;
                if ((*apb2enr & need) != need)
                {
                    /* Verify-after-write with bounded retries: the enable
                     * write sits right before heavy BTE/SDRAM bursts whose
                     * current sag corrupts it (observed 0x501D landing as
                     * 0x4005 -- exactly the IOPC|SPI1 bits dropped), so a
                     * blind single write never sticks. */
                    uint8_t tries = 0u;
                    do {
                        *apb2enr |= need;
                        for (volatile uint32_t q = 0u; q < 200u; q++) {}
                    } while ((*apb2enr & need) != need && ++tries < 16u);
                    static uint32_t last_clk_report;
                    uint32_t now_c = HAL_GetTick();
                    if ((now_c - last_clk_report) >= 5000u)
                    {
                        last_clk_report = now_c;
                        hal_uart_send_text("[CLK] lost, restored\r\n");
                    }
                }
            }
            {
                static bool painted;
                if (!painted)
                {
                    volatile uint32_t *p;
                    for (p = WDT_STACK_LOW; p < WDT_STACK_LOW + 16u; p++)
                        *p = WDT_STACK_CANARY;
                    painted = true;
                }
                else if (WDT_STACK_LOW[0] != WDT_STACK_CANARY ||
                         WDT_STACK_LOW[15] != WDT_STACK_CANARY)
                {
                    hal_uart_send_text("[STK] overflow! gap=");
                    perf_send_u32(gap);
                    hal_uart_send_text("\r\n");
                }
            }
            if (gap <= 50u)
                s_loop_stall_printed = 0u;
            s_loop_last_tick = now_loop;
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
    /* A silent __disable_irq()+loop here is indistinguishable from any
     * other freeze. Report the CALLER's return address: whoever detected
     * the HAL failure is the source of the runtime shutdown. */
    unsigned long err_lr = 0u;
    __asm volatile ("mov %0, lr" : "=r"(err_lr));
    hal_uart_send_text("[ERR] lr=");
    hal_uart_send_hex8((uint8_t)(err_lr >> 24));
    hal_uart_send_hex8((uint8_t)(err_lr >> 16));
    hal_uart_send_hex8((uint8_t)(err_lr >> 8));
    hal_uart_send_hex8((uint8_t)err_lr);
    hal_uart_send_text("\r\n");
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
