# Display Refresh and Trend Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除单位切换导致的 1.7 秒趋势全量重建，保证读数更新优先，并建立可区分输入速率、读数渲染率和页面提交率的验证闭环。

**Architecture:** 保留 LT7680 双页原子提交模型，但把“数据变化”“轴结构变化”和“页面提交”拆成独立状态。运行帧继续在隐藏页合成，趋势轴变化只更新轴带和需要重映射的趋势列；读数/状态区域拥有更高调度优先级。任何需要写两页或整页复制的方案必须先通过硬件耗时测试，不得作为默认路径。

**Tech Stack:** STM32F103C8T6 bare-metal/CubeMX、LT7680A-R GE/BTE、C11、Ninja/CMake、宿主 GCC 测试、Node.js 仿真验证、OpenOCD CMSIS-DAP。

## Global Constraints

- 真机烧录唯一来源是 `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`。
- 正式固件必须保持 `bte-miss=0`、`missed=0`。
- `firmware/src/` 与 `KEITHLEY_2000_LCD/Core/{Src,Inc}/` 的纯逻辑文件必须逐字节同步。
- 不把 `500 Read/s` 状态文案当作 TFT 刷新率；必须分别报告 `input_hz`、`reading_frames`、`display_commits` 和 `trend_axis_rebuilds`。
- 默认 Demo 输入周期仍为 `DEMO_SAMPLE_PERIOD_MS=100u`，除非新增独立的可配置高频测试模式并证明不会阻塞渲染。
- 不得恢复每个 GE 原语同时写两个页面的“可见页穿透”行为作为趋势重建方案。
- 不得使用整页 BTE copy 作为每个运行帧的隐藏页同步方案；此前实测会将提交率降至约 `2~3fps`。
- 每个阶段必须先离线构建和测试，再烧录；首次异常必须读取 PC、VTOR、CFSR/HFSR 后再继续。
- 所有临时日志必须使用 `[DEBUG-...]` 前缀，并在最终提交前完全删除。

---

### Task 1: 建立刷新率与重建率反馈环

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c:247-285, 610-660, 2130-2235`
- Test: `firmware/tests/test_render_scheduler.c`
- Test: `sim/verify.js`

**Interfaces:**
- Produces runtime counters `input_hz`, `reading_frames`, `display_commits`, `trend_axis_rebuilds`, `trend_column_updates`, `missed`, and `bte_miss` in the existing `PERF` line.
- Keeps the existing `PERF fps=... frame-ms=...` fields backward compatible.

- [ ] **Step 1: Add a deterministic counter seam before changing rendering.**

Add a small counter group in `main.c`:

```c
static uint32_t s_perf_fields_window;
static uint32_t s_perf_reading_frames_window;
static uint32_t s_perf_display_commits_window;
static uint32_t s_perf_axis_rebuilds_window;
static uint32_t s_perf_trend_columns_window;
```

Increment them only at these exact boundaries:

```c
/* proto_on_event(), K2000_EVT_FIELD */

/* reading_scene_render(), after begin_hidden_frame() accepts a reading dirty bit */

/* display_enable_after_initial_frame(), immediately after present_page() succeeds */

/* reading_scene_render(), after the final repaint suppression decision */
if (s_trend_full_repaint)
    s_perf_axis_rebuilds_window++;

/* RENDER_PHASE_UPDATE_TREND_COLUMNS, once per actual trend_draw_column() call */
```

- [ ] **Step 2: Run the existing host and simulator checks.**

Run:

```bash
(cd firmware && ./tests/run_tests.sh)
node sim/verify.js
```

Expected: all existing tests pass; no behavior change.

- [ ] **Step 3: Build, flash, and capture a 25-second baseline.**

Run:

```bash
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
openocd -f openocd.cfg -c "init" -c "halt" -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" -c "reset" -c "shutdown"
```

Capture `/tmp/k2000-refresh-baseline.log` through `/dev/ttyACM0` for 25 seconds. The red baseline is expected to show roughly `input_hz=10`, `display_commits=31`, and one or more `trend_axis_rebuilds` during unit rotation.

- [ ] **Step 4: Commit the measurement seam separately.**

```bash
git add KEITHLEY_2000_LCD/Core/Src/main.c
git commit -m "test: measure input and display refresh stages"
```

---

### Task 2: Separate trend-axis state from trend-data updates

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c:2040-2075, 2165-2245`
- Modify: `firmware/src/main_display.c:188-224` only if the pure logic API changes
- Modify: `KEITHLEY_2000_LCD/Core/Src/main_display.c` only if the pure logic API changes
- Modify: matching `main_display.h` files if a public frame field is added
- Test: `firmware/tests/test_main_display.c`

**Interfaces:**
- Produces a stable `trend_axis_identity` consisting of `unit`, `step`, and `top`.
- Keeps `trend_minimum` and `trend_maximum` as data bounds, separate from the stable display bounds.
- No axis rebuild is requested merely because the sliding window min/max changed within the active unit.

- [ ] **Step 1: Add a failing pure-logic test for stable axis identity.**

Extend `firmware/tests/test_main_display.c` with two frames from the same unit and nearby ranges. Assert that the display axis identity remains unchanged while the data bounds differ:

```c
main_display_frame_t first;
main_display_frame_t second;

trend_buffer_reset(&trend);
assert(trend_buffer_add(&trend, 1000u, "1.20000", "VDC"));
main_display_format_trend(&trend, 1000u, "VDC", &first);

assert(trend_buffer_add(&trend, 1020u, "1.21000", "VDC"));
main_display_format_trend(&trend, 1020u, "VDC", &second);

assert(strcmp(first.trend_axis_unit, second.trend_axis_unit) == 0);
assert(first.trend_axis_step == second.trend_axis_step);
assert(first.trend_axis_top == second.trend_axis_top);
```

- [ ] **Step 2: Run the focused test and verify it fails or exposes the current contract.**

Run:

```bash
(cd firmware && ./tests/run_tests.sh)
```

Expected: the new assertion must exercise the current axis contract. If it passes already, keep the test as a regression guard and place the stability state in `main.c`, where page history is available.

- [ ] **Step 3: Define explicit axis acceptance rules in `main.c`.**

Use these rules in the final repaint decision:

```text
new unit != resident unit       -> axis rebuild allowed immediately
resident axis is uninitialized   -> axis rebuild allowed once
same unit and data inside axis   -> no axis rebuild
same unit and data outside axis  -> mark axis candidate, do not rebuild yet
candidate persists for 10 seconds -> axis rebuild allowed
```

Store the candidate state explicitly rather than overloading `s_trend_full_repaint`:

```c
    bool valid;
    char unit[8];
    float step;
    float top;
    uint32_t first_seen_ms;
} trend_axis_candidate_t;
```

- [ ] **Step 4: Make trend columns use the resident axis bounds.**

Before `trend_draw_column()` is scheduled, set the frame projection bounds from the resident axis:

```c
float scale = trend_buffer_display_scale(&s_trend);
    (resident_top - 3.0f * resident_step) / scale;
```

Clamp computed column coordinates to `0..MAIN_DISPLAY_PLOT_H - 1` before converting to `uint8_t`.

- [ ] **Step 5: Run focused tests, then the complete offline suite.**

Run:

```bash
(cd firmware && ./tests/run_tests.sh)
node sim/verify.js
```

Expected: all tests pass, axis labels remain unit-aware, and same-unit range drift does not require a new axis identity.

- [ ] **Step 6: Commit the axis state change.**

```bash
```

---

### Task 3: Implement true hidden-page rendering without per-frame full copy

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c:720-785, 570-608`
- Modify: `KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h` only if a rectangle copy API is required
- Modify: `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c` only if a rectangle copy API is required
- Test: `firmware/tests/test_render_scheduler.c`
- Hardware validation: OpenOCD + LCD panel

**Interfaces:**
- Produces `ui_fill_rect_hidden()`, `ui_draw_line_hidden()`, or an equivalent internal path that writes only `s_render_page` during runtime composition.
- Keeps initialization and any explicitly required dual-page synchronization separate from normal runtime rendering.
- Must not call `lt7680_gfx_copy_page()` on every frame.

- [ ] **Step 1: Define the page invariant in code comments and a scheduler test.**

The invariant is:

```text
At begin_hidden_frame(), s_render_page contains the last committed scene plus
all changes not yet committed. Runtime GE/BTE writes target only s_render_page.
After present_page(), s_visible_page becomes s_render_page.
```

Add scheduler coverage that an active hidden frame remains active until its phase completes and that a pending reading update is not restarted by a trend request.

- [ ] **Step 2: Add dirty-region synchronization instead of full-page copy.**

Track the regions modified in the current frame:

```c
#define FRAME_REGION_STATUS  0x01u
#define FRAME_REGION_READING 0x02u
#define FRAME_REGION_TREND   0x04u
```

When a new hidden page is selected, copy only regions that can differ from the previous committed page. Use LT7680 BTE rectangle copy with source and target strides of the full canvas. Do not copy the entire `320x960` canvas on every frame.

- [ ] **Step 3: Validate one region at a time on hardware.**

First test only a reading-region copy. Verify:

```text
normal reading updates remain visible
status/trend regions remain unchanged
bte-miss=0
no visible page blanking
```

Then add status and trend regions separately. If a rectangle copy produces artifacts, stop and record the register geometry before changing the GE path.

- [ ] **Step 4: Remove dual-page primitive writes only after region sync passes.**

Change `ui_fill_rect()` and `ui_draw_line()` to write `s_render_page` only. Keep the original dual-page path available behind a temporary compile-time switch until the hardware comparison passes:

```c
#ifndef LT7680_RUNTIME_HIDDEN_ONLY
#define LT7680_RUNTIME_HIDDEN_ONLY 0
#endif
```

The switch must be removed before final commit after the hidden-only path is validated.

- [ ] **Step 5: Measure against the baseline.**

Required acceptance:

```text
display_commits >= 25/s in steady state
reading_frames follows input events without multi-second gaps
no visible clearing of the scanned page
bte-miss=0
missed=0
```

- [ ] **Step 6: Commit the hidden-page change.**

```bash
```

---

### Task 4: Give readings priority over trend work

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/render_scheduler.c:3-17, 29-71`
- Modify: `KEITHLEY_2000_LCD/Core/Inc/render_scheduler.h:6-27`
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c:2140-2260, 2560-2690`
- Mirror: `firmware/src/render_scheduler.c`, `firmware/src/render_scheduler.h`
- Test: `firmware/tests/test_render_scheduler.c`

**Interfaces:**
- Adds a pending low-priority trend flag that cannot preempt `RENDER_DIRTY_STATUS` or `RENDER_DIRTY_READING`.
- Adds a bounded trend-column budget per scheduler turn.
- Reading updates remain coalesced to the newest frame instead of queuing every intermediate numeric value.

- [ ] **Step 1: Write the failing scheduler test.**

Add a test that queues trend work, then queues a reading update, and asserts reading is selected first:

```c
render_scheduler_t scheduler;
render_scheduler_init(&scheduler);
render_scheduler_request_trend(&scheduler);
render_scheduler_request_regions(&scheduler, RENDER_DIRTY_READING);
assert(scheduler.phase == RENDER_PHASE_UPDATE_READING);
```

- [ ] **Step 2: Implement priority selection.**

`select_pending()` must remain ordered:

```c
RENDER_DIRTY_STATUS
RENDER_DIRTY_READING
trend_pending
IDLE
```

When a reading event arrives while trend work is active, set a pending region bit; do not restart the active bitmap job, and do not discard the newest `s_frame` snapshot.

- [ ] **Step 3: Bound trend work.**

Keep the existing `budget=8u` column limit, but ensure completion of one trend slice returns control to the main loop. A trend-axis redraw must not execute all 240 columns before the next reading snapshot can be accepted.

- [ ] **Step 4: Add a separate high-rate input test mode.**

Do not change the default Demo behavior. Add a compile-time mode with explicit semantics:

```c
#define K2000_DEMO_INPUT_HZ 10u
```

Use `K2000_DEMO_INPUT_HZ=500` only for input-path testing, while display submission remains coalesced to the newest value. Report:

input_hz       = generated/received fields per second
reading_frames  = reading render snapshots accepted per second
display_fps     = completed page commits per second
```

- [ ] **Step 5: Run the high-rate test without the trend graph first.**

Temporarily disable trend requests in the test build, run the 500Hz input mode, and verify:

```text
input_hz >= 450
reading_frames remains bounded by the renderer budget
display_fps remains stable
missed=0
```

Then re-enable trend work and verify that input processing remains above the agreed threshold while trend columns progress in the background.

- [ ] **Step 6: Commit the scheduler priority change.**

```bash
```

---

### Task 5: Remove instrumentation, run acceptance, and document the rate contract

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`
- Modify: `firmware/src/*` mirrors touched by Tasks 2 and 4
- Modify: `docs/adr/0002-independent-scenes.md` with the final refresh-rate contract
- Test: `firmware/tests/run_tests.sh`
- Test: `sim/verify.js`

**Interfaces:**
- Final firmware has no temporary `[DEBUG-*]` logs.
- The regular `PERF` line uses stable field names for input, reading, display, and trend rates.

- [ ] **Step 1: Remove temporary counters and logs.**

Run:

```bash
```

Expected: no output. Keep only intentionally supported `PERF` telemetry fields.

- [ ] **Step 2: Synchronize mirrored pure-logic files.**

Run:

```bash
diff -q firmware/src/render_scheduler.c KEITHLEY_2000_LCD/Core/Src/render_scheduler.c
```

Expected: all commands produce no output.

- [ ] **Step 3: Run all offline validation.**

```bash
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
(cd firmware && ./tests/run_tests.sh)
node sim/verify.js
```

Expected:

```text
17 PASS
sim/verify.js: ALL CHECKS PASS
no compiler warnings/errors
```

- [ ] **Step 4: Flash and run the complete hardware acceptance loop.**

```bash
openocd -f openocd.cfg \
  -c "init" -c "halt" \
  -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" \
  -c "reset" -c "shutdown"
```

Run the board for at least 60 seconds and record:

```text
input_hz
reading_frames
display_fps
trend_axis_rebuilds
max frame time
missed
bte-miss
```

Acceptance target:

```text
same-unit trend_axis_rebuilds = 0
no multi-second gaps in reading_frames
display_fps >= 25 in steady state
input_hz >= 450 only in the explicit 500Hz test mode
missed = 0
bte-miss = 0
```

- [ ] **Step 5: Update the ADR and commit the final cleanup.**

Document that `500 Read/s` is an input/measurement-rate label, while TFT display commits are bounded by the cooperative renderer and panel transfer budget. Then commit:

```bash
```

## Self-Review

- **Spec coverage:** The plan measures and separates input rate, reading rendering, display commits, trend-axis rebuilds, and long frames; fixes axis-triggered rebuilds; fixes visible-page write-through; prioritizes readings; adds a separate 500Hz input test mode; and defines final hardware acceptance criteria.
- **No placeholders:** Every implementation step names files, functions, thresholds, commands, and expected results.
- **Type consistency:** `trend_axis_candidate_t`, render-region masks, scheduler pending bits, and PERF field meanings are defined before later tasks consume them.
- **Known seam limitation:** There is no existing host-side test seam that reproduces LT7680 page visibility or GE timing. The plan explicitly requires hardware validation for Tasks 3 and 5; scheduler and axis identity behavior remains covered by offline tests.
