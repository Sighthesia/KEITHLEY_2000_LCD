# Top Bar And Range UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重整顶部两行 UI，显示品牌、内部温度、运行时间、激活状态和两行档位名称。

**Architecture:** 扩展共享 `main_display_frame_t` 作为纯格式化快照，CubeMX 主循环负责 ADC 内部温度采样和 tick 时间，硬件渲染只消费快照。仿真器复刻同一布局语义，读数区和趋势区几何保持不变。

**Tech Stack:** STM32F103 HAL/C、宿主 gcc 测试、单文件 HTML Canvas 仿真器、Node.js 验证脚本。

## Global Constraints

- 保持 UI 逻辑尺寸 960x320、读数区 y=50、趋势区 y=192。
- 共享逻辑文件 `firmware/src` 与 CubeMX `Core` 对应文件逐字节同步。
- 内部温度使用 STM32F103 ADC1 通道 16；运行时间使用 `HAL_GetTick()`。
- 不增加大数字字形或大块静态数据，遵守当前 Flash 紧张约束。

### Task 1: Shared Display Snapshot

**Files:**
- Modify: `firmware/src/main_display.h`
- Modify: `firmware/src/main_display.c`
- Modify: `firmware/tests/test_main_display.c`
- Sync: `KEITHLEY_2000_LCD/Core/Inc/main_display.h`
- Sync: `KEITHLEY_2000_LCD/Core/Src/main_display.c`

**Interfaces:**
- Add `main_display_format_runtime(main_display_frame_t *, uint16_t temp_tenths_c, uint32_t uptime_ms)`.
- Add frame strings `brand`, `temperature`, `uptime`, `range_line1`, `range_line2`, `active_status`.

- [ ] Add failing assertions for `KEITHLEY 2000`, `DC`/`VOLTAGE`, formatted `25.0°C`, `00:01:02`, and only-active status aggregation.
- [ ] Implement bounded ASCII formatters and a runtime snapshot function that calls the existing model formatter first.
- [ ] Make status aggregation include only active labels separated by one space; no active label produces an empty string.
- [ ] Split the existing function label at the first space into two lines, with the second line containing the remainder.
- [ ] Run `firmware/tests/run_tests.sh` and commit `feat(ui): add runtime top bar snapshot`.

### Task 2: Hardware Top Bar And ADC

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`
- Modify: `KEITHLEY_2000_LCD/Core/Inc/main_display.h`
- Modify: `KEITHLEY_2000_LCD/Core/Src/main_display.c`

**Interfaces:**
- Use ADC1 channel 16 through direct HAL/register setup local to `main.c`.
- Refresh the snapshot with `HAL_GetTick()` before scheduling a display update.

- [ ] Add ADC1 clock/configuration for channel 16 with a long sample time and enable the internal temperature path.
- [ ] Convert ADC raw data using `V25=1.43V`, `Avg_Slope=4.3mV/C`, and measured VREF compensation; clamp invalid readings to `--.-°C`.
- [ ] Replace the status renderer with one first-line pass: dark fill, brand at x=12, active status centered in remaining space, temperature and uptime right-aligned.
- [ ] Replace the second info bar with a borderless fill and two centered range-name text calls at x=12.
- [ ] Remove old Zin/Range/Rate/info capsule rendering and its dirty-row bookkeeping from the active baseline path.
- [ ] Schedule top-bar refresh once per second for uptime/temperature and keep status refresh content-driven.
- [ ] Build `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf` and commit `feat(display): render runtime top bar and range name`.

### Task 3: Simulator And Regression Checks

**Files:**
- Modify: `sim/index.html`
- Modify: `sim/verify.js`

- [ ] Change the Canvas layout to first-line brand/status/runtime and second-line borderless two-line range name.
- [ ] Use a monotonic page-start timestamp for `HH:MM:SS` and a deterministic simulated internal temperature around 25°C.
- [ ] Render status labels only when active; keep all inactive labels absent.
- [ ] Add checks for brand, runtime format, borderless second row, and range split while retaining trend geometry checks.
- [ ] Run `node sim/verify.js` and commit `test(sim): verify top bar and range layout`.

### Task 4: Final Synchronization And Full Verification

**Files:**
- Sync: `firmware/src/main_display.{c,h}` to CubeMX counterparts if any drift remains.

- [ ] Run `diff -u firmware/src/main_display.h KEITHLEY_2000_LCD/Core/Inc/main_display.h` and the equivalent C diff.
- [ ] Run `firmware/tests/run_tests.sh`.
- [ ] Run `node sim/verify.js`.
- [ ] Run the authoritative CubeMX Release build.
- [ ] Inspect `git status --short` and commit only intended files with `chore(ui): finalize top bar synchronization` if synchronization changes remain.
