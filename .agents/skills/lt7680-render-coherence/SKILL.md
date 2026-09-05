---
name: lt7680-render-coherence
description: Trigger when working with LT7680A-R staged GE/BTE rendering, header/info bars, RIF tile cache, dual-page divergence, or live stat refresh where per-second ticks, unit changes, or band sync cause flicker/stall.
---

# Skill: LT7680 Staged Rendering Coherence

## Problem & Symptoms
- **Context**: LT7680A-R drives 320x960 panel via 960x320 UI transpose. Rendering is cooperative: `reading_only_render_status_bar` / `info_panel` / `trend_background` are staged across main-loop calls (`idx` + resumable `bitmap_job`). Header shows brand + active_status + temperature + uptime (every 1s); second row shows档位/function + Zin/Range/Rate/lamps.
- **Symptoms / Error Signature**: `top row long flicker + global stall every second`; `severe stall and FPS drop on VDC/mVDC/AAC/VAC range switch`; `top/info bar occasional flicker with vertical gaps or half-clipped glyphs`.
- **Root Cause Trap**:
  1. Full-bar fill + full redraw of header on every second tick blocks reading/trend: brand/active/temp/uptime each need ~8 slices (64-rect budget), total ~40 slices ≈ 250ms stall.
  2. Resumable `ui_draw_text` inside same `idx` refills background on every resume, erasing half-drawn glyphs → vertical half-clip.
  3. Per-field incremental header leaves inter-field gaps black on first boot if full-bar fill is skipped → vertical lines.
  4. Unit change (V↔mV, DC↔AC) changes `trend_buffer_display_scale` and `trend_axis_range`, invalidating `s_reading_only_page_trend_bg_valid` and RIF dir cache, triggering 19-step trend background rebuild + reading-band clear in same frame as header → refresh collapse.
  5. Header band `y24 h26` sits in gap between `FRAME_REGION_STATUS (0/24)` and `FRAME_REGION_READING (50/142)`, so `hidden_page_sync_regions` never copies it → alternating pages show old/new header → flicker if visible-page writes occur.

## Correct Pattern (Do This)
```c
// 1. Per-field dirty + first-boot full fill, guard fill on resume
static bool reading_only_render_status_bar(void){
  static uint8_t idx;
  bool brand_dirty = !valid || strcmp(page_brand, s_frame.brand)!=0;
  // active/temp/uptime similarly
  if (s_reading_only_stage != READING_ONLY_STATUS) idx=0;
  if (idx==0){
    if (!valid){ // first boot gaps
      if (ui_fill_rect(0,0,960,24,BAR)!=OK) return false;
      idx++; return false;
    } else idx++;
  }
  if (idx==1){
    if (!brand_dirty) idx++;
    else {
      if (!s_bitmap_job.active){ // guard: don't refill on resume
        uint16_t fw = max(strlen(new), strlen(old))*12;
        ui_fill_rect(12,0,fw,24,BAR);
      }
      if (!ui_draw_text(12,0,s_frame.brand,WHITE)) return false;
      idx++; return false;
    }
  }
  // active/temp/uptime same pattern: fill only when !active, draw, idx++, return false
  // fall-through allows skipping clean fields in same call
  idx=0; /* commit page caches: brand/active/temp/uptime + lamps */ return true;
}

// 2. Drive header once per second without blocking reading
if ((HAL_GetTick()-s_temperature_tick)>=1000u) s_reading_only_dirty=true; // not s_ui_dirty_regions
// IDLE decides status_need/info_need via page-cache compare, enters STATUS only if dirty

// 3. On unit change, invalidate trend pages and defer rebuild
if (strcmp(prev_unit, trend_buffer_display_unit(&s_trend))!=0){
  reading_only_invalidate_trend_pages(); // clears bg_valid, keeps stats visible
  // trend background rebuild runs in READING_ONLY_TREND stage, not in same frame as reading
}
```

## Anti-Pattern (Don't Do This)
```c
// Full redraw every second
if (idx==0) ui_fill_rect(0,0,960,24,BAR);
ui_draw_text(12,0,brand,WHITE);
ui_draw_text(center,0,active,GREEN);
ui_draw_text(right,0,temp,CYAN);
ui_draw_text(right+...,0,uptime,WHITE); // 4 texts every second → stall

// Refill on every resume
if (brand_dirty){
  ui_fill_rect(12,0,fw,24,BAR); // executed again on resume
  ui_draw_text(12,0,brand,WHITE); // half-drawn glyphs erased → clip
}

// Gap leaves black
// initial page cleared to 0x0000, per-field fills only cover text widths → inter-field stays black → vertical lines

// Unit change does heavy work inline
main_display_format_trend(...); keithley_trend_axis_range(...); rif_init(); // in reading path → same frame stall
```

## Dual-page coherence rules (2026-09 session)

Two SDRAM pages flip every frame. Any pixel that differs between pages
flickers at flip rate. Rules, in order:

1. **Every cut point identical.** Any direct draw outside the region
   mechanism must dual-write (`s_trend_sweep_drawing`-style gate) or join
   the region flags. No exceptions.
2. **Partial dual is worse than none.** Dual-writing chrome while clearing
   single-page leaves stale text under new text (overlap), stale labels
   (frozen) and a stale plot (flicker) on the sibling — while shared-valid
   stops it ever rebuilding. Dual-write whole passes or nothing.
3. **Shared snapshot for time-varying text.** A/B pages build one frame
   apart; a sliding window mints different last digits per page and every
   flip flickers between two near-identical values. First page to build
   captures (`s_trend_stat_snapshot`), sibling repaints from it; invalidate
   clears it. Publish snapshot only after successful paint/sync, never
   before — a failed sync must retry, not diverge.
4. **One `ui_draw_text` per idx step.** The bitmap job is shared and
   resumable: a second call in the same step re-tasks it forever (black
   screen, frame never commits). Per-glyph loops must use throwaway local
   `bitmap_job_t`, never the shared job.
5. **Union-erase needs a valid old extent.** Erasing new∪old with an empty
   cache computes a garbage old origin and wipes just-drawn content (brand
   reduced to "KE"). Guard with `old_len > 0`, else erase new extent only.
6. **Erase exactly the content band.** 2 px over-height smears BAR ticks
   onto the adjacent black plot with every changed glyph.
7. **Yield needs an armed-once-per-frame flag.** Suspend-trend-for-reading
   without it re-suspends on every resume at 500 Hz (CLEAR detour outlasts
   the throttle) → PRESENT unreachable → frozen display, silent serial.
   Also gate suspend on both jobs idle, or the resume chain continues a
   stale job (wrong text, poisoned caches).
8. **Scope the dual-write gate to the turn, never the pass.** The global
   dual flag (`s_trend_sweep_drawing`) asserted across a multi-visit live
   pass dual-writes every unrelated STATUS/INFO/VALUE stage in between
   straight onto the visible page — rows visibly paint item-by-item on
   every rotation (user-verified). Live stays hidden-page single; the
   sibling converges via the shared snapshot pass. Sweep keeps its
   turn-scoped dual (explicit design).

## Live stat refresh design (2026-09 session)

- **Cadence ≠ volume.** At 500 Hz input the window stats differ on every
  pass, so cadence only sets how often a repaint happens. 10 Hz burned
  250 ms/s in hitches; 1 Hz burns ~25 ms/s for one stretched frame.
  Measure first (`live=25~30 ms/visit` pre-fix); pick cadence from data.
- **Budgeted diff:** ≤4 changed glyphs per visit (each erase+draw ≈ 9 GE
  fills ≈ 3 ms single-page), resume via `*pos` next visit; snapshot
  publishes per completed slot.
- **Decouple pass from present:** one bounded slice per frame, always
  present, resume next frame. Dual-page + per-slot snapshot publish keep
  every cut point identical (worst case: single-glyph single-frame tear,
  self-healing, invisible).
- **Prefer BTE strip copy over dual text** when a whole strip changes
  (~3 fast ops vs ~2x per-run fills; measured copy 3~4 ms). Delete the
  helper if dual covers the path — never keep both.
- Stats precision follows the reading decimals (fixed-decimal helper, no
  float printf); axis/range labels keep axis integers.

## Perf diagnosing playbook (2026-09 session)

- **Feedback loop:** build ONLY `KEITHLEY_2000_LCD/build/Release/...elf`
  (top-level `build/` is stale, do not flash); flash via halt sequence,
  never `program ... reset exit`; `stty -F /dev/ttyACM0 115200 raw -echo`
  then `PERF`/`gap`: healthy = `input_hz≈500, missed=0, fps≈30,
  frame-ms~10, gap<40, reading_errors=0`. Plus `node sim/verify.js` and
  `firmware/tests/run_tests.sh`.
- **Known costs, measured on-target:** UART PERF line ~450 B ≈ 35–40 ms
  blocking per print (print every 5th window, count every window);
  MRWDP peek per slot ≈ ms-scale (gate `TREND_SWEEP_PROBE` off except
  when chasing landing faults); Flash-DMA glyph ≈ 7 ms vs SDRAM-cache
  BTE blit ≈ 1 ms (`READING_ONLY_DIRECT_DMA` must stay 0); full header
  repaint ≈ 1400 GE fills; text run ≈ 0.3 ms/fill.
- **Demo-rate discipline:** 500 Hz sustained + 30 fps display. Catch-up
  budget must cover ~17 samples/turn at 30 fps (64 used). Acceptance is
  `input_hz≈500, missed=0` — check this before suspecting the renderer.
- **Instrumentation discipline:** tag every debug print/counter with a
  unique prefix (`[DBGn]`), verify with a single grep it reaches zero
  before commit. Never commit instrumentation.
- Full story: `docs/adr/0007-trend-header-taskbar-axes.md`.

## Verification & Diagnostic Checklist
- [ ] `grep -n "s_bitmap_job.active" Core/Src/main.c` shows fill guarded before each `ui_draw_text` in header.
- [ ] `grep -n "reading_only_page_brand" Core/Src/main.c` shows per-field cache and first-boot full fill at `idx==0`.
- [ ] Second tick draws only uptime: set `brand/active/temp` constant, change uptime by 1s, count `ui_fill_rect` calls = 1 (uptime width) not 4.
- [ ] Range switch VDC→mVDC: `trend_buffer_display_scale` changes, `s_reading_only_page_trend_bg_valid` cleared, rebuild runs in `READING_ONLY_TREND` slices (<8 cols/slice), FPS stays ~10 not collapsed.
- [ ] No visible-page writes during trend: `READING_ONLY_PAGE_FLIP` no longer forces `s_render_page = s_visible_page`; header/info always on hidden page.
- [ ] Build: `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf` no `Werror`; `node sim/verify.js` and `firmware/tests/run_tests.sh` pass.
