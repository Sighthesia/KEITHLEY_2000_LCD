# Reading-Baseline Trend Chart Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 500Hz 输入 / 30Hz 主读数基线上接入趋势图：单位切换时建一次静态背景，之后每帧只增量更新变化的曲线列。

**Architecture:** 纯逻辑层新增线性 Y 标签与列投影辅助函数；CubeMX `reading_only_render()` 在 `SUFFIX` 与 `PRESENT` 之间增加 `TREND` 阶段。不调用 `trend_axis.c`，不恢复旧全场景调度。

**Tech Stack:** STM32F103C8T6 / LT7680A-R GE、C11、宿主 gcc 测试、CMake/Ninja、OpenOCD CMSIS-DAP。

## Global Constraints

- 真机烧录唯一来源是 `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`。
- 保留 `K2000_READING_ONLY_BASELINE=1`。
- `firmware/src/` 与 `KEITHLEY_2000_LCD/Core/{Src,Inc}/` 的纯逻辑文件必须逐字节同步（手工 `cp` 后 `diff`）。
- 正式固件稳态（窗口已满且该秒无单位切换）：`input_hz >= 450`、`reading_frames >= 28`、`display_commits >= 28`、`missed=0`、`reading_errors=0`。
- 不启用 `trend_axis.c` 的 1/2/5 驻留轴；曲线 Y 用窗口 min/max 线性压缩。
- 不得恢复双页同时写或整页 BTE copy。
- 不改 RIF / 大字 / 状态栏 DMA。
- 临时日志用 `[DEBUG-...]` 前缀，最终提交前删除。

---

### Task 1: 线性 Y 标签与列投影（纯逻辑）

**Files:**
- Modify: `firmware/src/main_display.h`
- Modify: `firmware/src/main_display.c`
- Modify: `KEITHLEY_2000_LCD/Core/Inc/main_display.h`（与 firmware 逐字节同步）
- Modify: `KEITHLEY_2000_LCD/Core/Src/main_display.c`（与 firmware 逐字节同步）
- Test: `firmware/tests/test_main_display.c`

**Interfaces:**
- Consumes: `trend_buffer_add` / `trend_buffer_range` / `trend_buffer_project` / `trend_buffer_display_scale` / `trend_buffer_display_unit`
- Produces:
  - `void main_display_format_linear_trend_labels(main_display_frame_t *frame);`
  - `uint8_t main_display_trend_plot_y(float value, float minimum, float maximum);`

- [ ] **Step 1: Write the failing tests**

在 `firmware/tests/test_main_display.c` 的 `main()` 末尾、`return 0` 之前加入：

```c
    {
        trend_buffer_t trend;
        main_display_frame_t frame;
        trend_column_t columns[TREND_MAX_COLUMNS];
        uint16_t n;
        uint8_t y_hi, y_lo;

        memset(&frame, 0, sizeof(frame));
        trend_buffer_init(&trend);
        assert(trend_buffer_add(&trend, 0u, "1.00", "VDC"));
        assert(trend_buffer_add(&trend, 200u, "3.00", "VDC"));
        main_display_format_trend(&trend, 200u, "VDC", &frame);
        assert(frame.trend_has_data);
        main_display_format_linear_trend_labels(&frame);
        assert(strstr(frame.y_labels[0], "VDC") != 0);
        assert(strstr(frame.y_labels[3], "VDC") != 0);
        /* Top label tracks window maximum, bottom tracks minimum. */
        assert(strstr(frame.y_labels[0], "3") != 0);
        assert(strstr(frame.y_labels[3], "1") != 0);

        y_hi = main_display_trend_plot_y(frame.trend_maximum,
                                         frame.trend_minimum,
                                         frame.trend_maximum);
        y_lo = main_display_trend_plot_y(frame.trend_minimum,
                                         frame.trend_minimum,
                                         frame.trend_maximum);
        assert(y_hi == 0u);
        assert(y_lo == (uint8_t)(MAIN_DISPLAY_PLOT_H - 1u));

        n = trend_buffer_project(&trend, 200u, columns, TREND_MAX_COLUMNS);
        assert(n == TREND_MAX_COLUMNS);
        {
            uint16_t i;
            bool any = false;
            for (i = 0u; i < n; i++)
            {
                if (!columns[i].occupied)
                    continue;
                any = true;
                assert(main_display_trend_plot_y(columns[i].maximum,
                                                 frame.trend_minimum,
                                                 frame.trend_maximum) <=
                       main_display_trend_plot_y(columns[i].minimum,
                                                 frame.trend_minimum,
                                                 frame.trend_maximum));
            }
            assert(any);
        }

        assert(trend_buffer_add(&trend, 400u, "1.00", "VAC"));
        assert(strcmp(trend_buffer_display_unit(&trend), "VAC") == 0);
        assert(trend_buffer_range(&trend, 400u, &frame.trend_minimum,
                                  &frame.trend_maximum));
        assert(frame.trend_minimum > 0.9f && frame.trend_maximum < 1.1f);
    }
```

- [ ] **Step 2: Run the focused test and confirm it fails to compile**

Run:

```bash
(cd firmware && ./tests/run_tests.sh)
```

Expected: `test_main_display` fails with implicit declaration / undefined reference to `main_display_format_linear_trend_labels` and `main_display_trend_plot_y`.

- [ ] **Step 3: Add declarations to both headers**

在 `firmware/src/main_display.h` 与 `KEITHLEY_2000_LCD/Core/Inc/main_display.h` 的 `main_display_set_trend_axis` 声明后追加：

```c
void main_display_format_linear_trend_labels(main_display_frame_t *frame);
uint8_t main_display_trend_plot_y(float value, float minimum, float maximum);
```

- [ ] **Step 4: Implement the helpers in `firmware/src/main_display.c`**

在 `format_fixed_axis` 之后加入：

```c
uint8_t main_display_trend_plot_y(float value, float minimum, float maximum)
{
    float span;
    float fy;

    span = maximum - minimum;
    if (span < 0.000001f)
        return (uint8_t)(MAIN_DISPLAY_PLOT_H / 2u);
    fy = (maximum - value) * (float)MAIN_DISPLAY_PLOT_H / span;
    if (fy < 0.0f)
        fy = 0.0f;
    if (fy > (float)MAIN_DISPLAY_PLOT_H - 1.0f)
        fy = (float)MAIN_DISPLAY_PLOT_H - 1.0f;
    return (uint8_t)fy;
}

void main_display_format_linear_trend_labels(main_display_frame_t *frame)
{
    float scale;
    float minimum;
    float maximum;
    float span;
    const char *unit;
    uint8_t i;

    if (frame == 0 || !frame->trend_has_data)
        return;
    unit = frame->trend_axis_unit[0] != '\0' ? frame->trend_axis_unit : "";
    scale = 1.0f;
    if (unit[0] == 'm')
        scale = 1000.0f;
    else if (unit[0] == 'u' ||
             ((uint8_t)unit[0] == 0xC2u && (uint8_t)unit[1] == 0xB5u))
        scale = 1000000.0f;
    else if (unit[0] == 'k')
        scale = 0.001f;
    else if (unit[0] == 'M' && unit[1] != '\0')
        scale = 0.000001f;
    minimum = frame->trend_minimum * scale;
    maximum = frame->trend_maximum * scale;
    span = maximum - minimum;
    if (span < 0.000001f)
        span = 0.000001f;
    for (i = 0u; i < MAIN_DISPLAY_Y_LABEL_COUNT; i++)
        format_fixed_axis(maximum - span * (float)i / 3.0f, unit, span / 3.0f,
                          frame->y_labels[i], MAIN_DISPLAY_AXIS_LABEL_MAX);
}
```

然后：

```bash
cp firmware/src/main_display.c KEITHLEY_2000_LCD/Core/Src/main_display.c
cp firmware/src/main_display.h KEITHLEY_2000_LCD/Core/Inc/main_display.h
diff -q firmware/src/main_display.c KEITHLEY_2000_LCD/Core/Src/main_display.c
diff -q firmware/src/main_display.h KEITHLEY_2000_LCD/Core/Inc/main_display.h
```

Expected: no diff.

- [ ] **Step 5: Run host tests**

```bash
(cd firmware && ./tests/run_tests.sh)
node sim/verify.js
```

Expected: all PASS，包括既有 1/2/5 `format_trend` 断言。

- [ ] **Step 6: Commit**

```bash
git add firmware/src/main_display.h firmware/src/main_display.c \
        KEITHLEY_2000_LCD/Core/Inc/main_display.h \
        KEITHLEY_2000_LCD/Core/Src/main_display.c \
        firmware/tests/test_main_display.c
git commit -m "feat: linear trend Y labels and plot mapping"
```

---

### Task 2: 接入 TREND 阶段骨架

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`

**Interfaces:**
- Consumes: `reading_only_stage_t`、`reading_only_render()`、`reading_only_abort_frame()`
- Produces: `READING_ONLY_TREND`；`SUFFIX` → `TREND` → `PRESENT`；`s_reading_only_page_trend_bg_valid[2]`

- [ ] **Step 1: Add stage enum and per-page trend flags**

在 `reading_only_stage_t` 中于 `READING_ONLY_SUFFIX` 与 `READING_ONLY_PRESENT` 之间插入 `READING_ONLY_TREND`。

在 `s_reading_only_page_info_lamps[2]` 后加入：

```c
static bool s_reading_only_page_trend_bg_valid[2];
static char s_reading_only_page_trend_unit[2][TREND_UNIT_ID_MAX];
static char s_reading_only_page_y_labels[2][MAIN_DISPLAY_Y_LABEL_COUNT][MAIN_DISPLAY_AXIS_LABEL_MAX];
static uint16_t s_reading_only_trend_column;
static bool s_reading_only_trend_bg_failed;
```

- [ ] **Step 2: Change SUFFIX to enter TREND**

把 `s_reading_only_stage = READING_ONLY_PRESENT;`（SUFFIX 成功路径）改为：

```c
s_reading_only_trend_column = 0u;
s_reading_only_trend_bg_failed = false;
s_reading_only_stage = READING_ONLY_TREND;
```

- [ ] **Step 3: Add a TREND case that currently only formats and yields to PRESENT**

在 `case READING_ONLY_SUFFIX` 与 `case READING_ONLY_PRESENT` 之间插入：

```c
    case READING_ONLY_TREND:
    {
        uint32_t now = HAL_GetTick();
        const char *unit;

        main_display_format_trend(&s_trend, now, s_frame.unit, &s_frame);
        if (s_frame.trend_has_data)
            main_display_format_linear_trend_labels(&s_frame);
        unit = trend_buffer_display_unit(&s_trend);
        if (unit[0] == '\0' ||
            strcmp(s_reading_only_page_trend_unit[s_render_page], unit) != 0)
        {
            s_reading_only_page_trend_bg_valid[0] = false;
            s_reading_only_page_trend_bg_valid[1] = false;
        }
        s_reading_only_stage = READING_ONLY_PRESENT;
        return;
    }
```

此步还不画图，只接通状态机并在单位变化时作废两页背景标志。

- [ ] **Step 4: Build CubeMX firmware**

```bash
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

Expected: link success。Flash 增加应很小。

- [ ] **Step 5: Commit**

```bash
git add KEITHLEY_2000_LCD/Core/Src/main.c
git commit -m "feat: add reading-only TREND stage"
```

---

### Task 3: 静态背景与轴标签

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`

**Interfaces:**
- Consumes: `trend_draw_background`、`trend_y_label_y`、`trend_x_label_x`、`ui_draw_text`、`ui_draw_line`、`ui_fill_rect`
- Produces: `reading_only_render_trend_background()` 返回 `true` 当本页背景+标签完成；失败返回 `false` 且不把半成品标 valid

- [ ] **Step 1: Un-legacy the background and label helpers**

去掉这些函数上的 `READING_ONLY_LEGACY`：

- `trend_restore_grid`
- `trend_y_label_y`
- `trend_draw_background`
- `trend_x_label_x`

保留 `trend_draw_column` 的 `READING_ONLY_LEGACY` 到 Task 4。

- [ ] **Step 2: Add a resumable background+label helper**

放在 `reading_only_render_info_panel()` 之后：

```c
static bool reading_only_render_trend_background(void)
{
    static uint8_t idx;
    const char *unit = trend_buffer_display_unit(&s_trend);

    if (s_reading_only_page_trend_bg_valid[s_render_page] &&
        strcmp(s_reading_only_page_trend_unit[s_render_page], unit) == 0)
        return true;
    if (idx == 0u)
    {
        if (ui_fill_rect(0u, MAIN_DISPLAY_CHART_PANEL_Y,
                         MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_CHART_PANEL_H,
                         MAIN_DISPLAY_COLOR_BAR) != LT7680_OK)
            return false;
        idx = 1u;
        return false;
    }
    if (idx == 1u)
    {
        if (ui_fill_rect(MAIN_DISPLAY_PLOT_X, MAIN_DISPLAY_PLOT_BG_Y,
                         MAIN_DISPLAY_PLOT_W, MAIN_DISPLAY_PLOT_BG_H,
                         MAIN_DISPLAY_COLOR_BG) != LT7680_OK)
            return false;
        idx = 2u;
        return false;
    }
    if (idx == 2u)
    {
        if (ui_fill_rect(MAIN_DISPLAY_PLOT_X, MAIN_DISPLAY_PLOT_DIVIDER_Y,
                         MAIN_DISPLAY_PLOT_W, MAIN_DISPLAY_PLOT_DIVIDER_H,
                         MAIN_DISPLAY_COLOR_BAR_ALT) != LT7680_OK)
            return false;
        idx = 3u;
        return false;
    }
    if (idx < 7u)
    {
        uint8_t i = (uint8_t)(idx - 3u);
        uint16_t y = (uint16_t)(MAIN_DISPLAY_PLOT_Y +
                                i * MAIN_DISPLAY_PLOT_H / 3u);
        if (ui_draw_line(MAIN_DISPLAY_PLOT_X, y,
                         MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W, y,
                         MAIN_DISPLAY_COLOR_GRID) != LT7680_OK)
            return false;
        idx++;
        return false;
    }
    if (idx < 12u)
    {
        uint8_t i = (uint8_t)(idx - 7u);
        uint16_t x = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                                i * MAIN_DISPLAY_PLOT_W / 4u);
        if (ui_draw_line(x, MAIN_DISPLAY_PLOT_Y, x,
                         MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H,
                         MAIN_DISPLAY_COLOR_GRID) != LT7680_OK)
            return false;
        idx++;
        return false;
    }
    if (idx < 16u)
    {
        uint8_t i = (uint8_t)(idx - 12u);
        size_t label_width = strlen(s_frame.y_labels[i]) * FONT_TEXT_WIDTH;
        uint16_t label_x = label_width + 4u <= MAIN_DISPLAY_PLOT_X
                               ? (uint16_t)(MAIN_DISPLAY_PLOT_X - 4u - label_width)
                               : 0u;
        if (s_frame.y_labels[i][0] != '\0' &&
            !ui_draw_text(label_x, trend_y_label_y(i), s_frame.y_labels[i],
                          MAIN_DISPLAY_COLOR_CYAN))
            return false;
        idx++;
        return false;
    }
    if (idx < 21u)
    {
        uint8_t i = (uint8_t)(idx - 16u);
        uint16_t x = (uint16_t)(MAIN_DISPLAY_PLOT_X +
                                i * MAIN_DISPLAY_PLOT_W / 4u);
        if (!ui_draw_text(trend_x_label_x(x, s_frame.x_labels[i]),
                          MAIN_DISPLAY_X_LABEL_Y, s_frame.x_labels[i],
                          MAIN_DISPLAY_COLOR_CYAN))
            return false;
        idx++;
        return false;
    }
    strncpy(s_reading_only_page_trend_unit[s_render_page], unit,
            TREND_UNIT_ID_MAX - 1u);
    s_reading_only_page_trend_unit[s_render_page][TREND_UNIT_ID_MAX - 1u] = '\0';
    memcpy(s_reading_only_page_y_labels[s_render_page], s_frame.y_labels,
           sizeof(s_reading_only_page_y_labels[0]));
    memset(s_drawn_trend_occupied[s_render_page], 0,
           sizeof(s_drawn_trend_occupied[0]));
    s_reading_only_page_trend_bg_valid[s_render_page] = true;
    idx = 0u;
    return true;
}
```

`idx` 是跨 `reading_only_render()` 调用的续绘游标。仅在这些时刻清零：helper 返回 `true`、背景失败放弃、或 `s_render_page` 与上次不同。不要每帧无条件清零。失败时 `s_reading_only_trend_bg_failed = true`、该页 `s_reading_only_page_trend_bg_valid = false`、`idx = 0u`。

- [ ] **Step 3: Add Y-label-only refresh when background is already valid**

若背景 valid 但 `memcmp(s_reading_only_page_y_labels[s_render_page], s_frame.y_labels, sizeof(s_frame.y_labels)) != 0`：

```c
if (ui_fill_rect(0u, MAIN_DISPLAY_Y_AXIS_Y, MAIN_DISPLAY_Y_AXIS_W,
                 MAIN_DISPLAY_Y_AXIS_H, MAIN_DISPLAY_COLOR_BAR) != LT7680_OK)
    return false;
/* then draw the four Y labels with the same coordinates as idx 12..15 */
```

此路径不重填绘图区、不重画 X 标签。完成后更新 `s_reading_only_page_y_labels[s_render_page]`。

- [ ] **Step 4: Call the helper from TREND, then go PRESENT**

`READING_ONLY_TREND` 在 format 之后：

```c
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
s_reading_only_stage = READING_ONLY_PRESENT;
```

此步仍不画曲线。背景失败不得调用 `reading_only_abort_frame()`。

- [ ] **Step 5: Build**

```bash
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

Expected: success。

- [ ] **Step 6: Commit**

```bash
git add KEITHLEY_2000_LCD/Core/Src/main.c
git commit -m "feat: draw trend background and axis labels once per unit"
```

---

### Task 4: 增量曲线列

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`

**Interfaces:**
- Consumes: `trend_buffer_project`、`main_display_trend_plot_y`、`s_drawn_trend_y0/y1/occupied`
- Produces: 去掉 `trend_draw_column` 的 `READING_ONLY_LEGACY`；函数改为返回 `bool`；TREND 阶段投影并差分绘制

- [ ] **Step 1: Rewrite `trend_draw_column` to return bool and use linear Y**

替换函数体（去掉 `READING_ONLY_LEGACY`）：

```c
static bool trend_draw_column(uint16_t column, bool erase_previous)
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
                : y0 - s_drawn_trend_y0[s_render_page][column]) < 2u &&
          (uint8_t)(s_drawn_trend_y1[s_render_page][column] > y1
                ? s_drawn_trend_y1[s_render_page][column] - y1
                : y1 - s_drawn_trend_y1[s_render_page][column]) < 2u)))
        return true;

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
```

- [ ] **Step 2: Drive columns from TREND after background succeeds**

背景 helper 返回 `true` 后：

```c
if (s_reading_only_trend_column == 0u)
    (void)trend_buffer_project(&s_trend, HAL_GetTick(), s_trend_columns,
                               TREND_MAX_COLUMNS);
/* Yield like VALUE glyphs: skip unchanged columns in this call, but
 * return after one column that issued GE so the main loop can drain
 * 500 Hz input. Resume via s_reading_only_trend_column. */
while (s_reading_only_trend_column < TREND_MAX_COLUMNS)
{
    uint16_t col = s_reading_only_trend_column;
    bool occupied_before = trend_drawn_occupied(col);
    uint8_t y0_before = s_drawn_trend_y0[s_render_page][col];
    uint8_t y1_before = s_drawn_trend_y1[s_render_page][col];

    if (!trend_draw_column(col, true))
    {
        if (s_reading_only_io_error)
        {
            s_reading_only_io_error = false;
            s_reading_only_stage = READING_ONLY_PRESENT;
        }
        return;
    }
    s_reading_only_trend_column++;
    if (occupied_before != trend_drawn_occupied(col) ||
        y0_before != s_drawn_trend_y0[s_render_page][col] ||
        y1_before != s_drawn_trend_y1[s_render_page][col])
        return;
}
if (s_trend_grid_dirty[s_render_page])
{
    trend_restore_grid(MAIN_DISPLAY_PLOT_X,
                       (uint16_t)(MAIN_DISPLAY_PLOT_X + MAIN_DISPLAY_PLOT_W),
                       MAIN_DISPLAY_PLOT_Y,
                       (uint16_t)(MAIN_DISPLAY_PLOT_Y + MAIN_DISPLAY_PLOT_H - 1u));
    s_trend_grid_dirty[s_render_page] = false;
}
s_reading_only_stage = READING_ONLY_PRESENT;
```

无数据时跳过列循环，不要画假线。未变化列在同一次调用里跳过；真正发了 GE 的列画完即 `return`，下一次主循环从下一列续。这样 500Hz 输入不会被 240 次线命令堵住，提交仍发生在同一逻辑帧的 `PRESENT`。

- [ ] **Step 3: Make `ui_draw_line` record `s_reading_only_io_error`**

与 `ui_fill_rect` 相同，在 `#if K2000_READING_ONLY_BASELINE` 下：

```c
if (st != LT7680_OK)
{
    s_reading_only_last_error = st;
    s_reading_only_io_error = true;
}
```

- [ ] **Step 4: Host tests still pass; build firmware**

```bash
(cd firmware && ./tests/run_tests.sh)
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

Expected: tests PASS；固件链接成功。

- [ ] **Step 5: Commit**

```bash
git add KEITHLEY_2000_LCD/Core/Src/main.c
git commit -m "feat: incremental trend columns on the reading baseline"
```

---

### Task 5: 失败策略与读数中止隔离

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`

**Interfaces:**
- Consumes: `reading_only_abort_frame()`、`READING_ONLY_TREND`
- Produces: 读数阶段失败仍丢整帧；TREND 失败走 PRESENT；abort 不把已 valid 的趋势背景无故打掉，除非失败发生在背景重建中

- [ ] **Step 1: Keep abort_frame from wiping trend background unless TREND was rebuilding it**

`reading_only_abort_frame` 只由 CLEAR/VALUE/UNIT/SUFFIX/PRESENT 调用。TREND 不得调用它。

若将来误调用，也不要 `memset` 趋势标志。不要在 abort 里清 `s_reading_only_page_trend_bg_valid`。

- [ ] **Step 2: PRESENT 仍在趋势失败后提交读数**

确认 `READING_ONLY_PRESENT` 不检查 `s_reading_only_trend_bg_failed`。失败后 `s_reading_only_dirty` 仍按 generation 比较；趋势背景 invalid 时下帧 TREND 会重来。

- [ ] **Step 3: Build**

```bash
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

- [ ] **Step 4: Commit only if Step 1–2 produced a diff**

```bash
git add KEITHLEY_2000_LCD/Core/Src/main.c
git commit -m "fix: keep reading commits when trend drawing fails"
```

若无 diff 则跳过 commit。

---

### Task 6: 烧录与真机验收

**Files:**
- None unless serial 显示缺陷需要补丁

- [ ] **Step 1: Flash**

```bash
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
openocd -f openocd.cfg \
  -c "init" -c "halt" \
  -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" \
  -c "reset" -c "shutdown"
```

- [ ] **Step 2: Capture serial**

```bash
openocd -f openocd.cfg -c "init" -c "reset run" -c "shutdown"
stty -F /dev/ttyACM1 115200 raw -echo
timeout 25s dd if=/dev/ttyACM1 bs=1 count=32768 status=none | cat -v
```

Expected:

- `INIT-OK`、`RIF BTE renderer=ON`
- 单位切换后可见 `frame-ms` 尖峰，随后稳态 `fps>=28`、`input_hz>=450`、`missed=0`、`reading_errors=0`
- `trend_column_updates` 在有数据后非零

- [ ] **Step 3: Visual check**

确认：

- 趋势区在 y192 以下，未盖住主读数和信息栏
- 曲线从右进入；空窗无假线
- 同档位三角波连续；单位切换后面板重建一次

- [ ] **Step 4: Commit any on-target fix separately**

缺陷补丁用独立 commit，不要 repeat Task 4 的 message。

---

## Spec coverage

| Spec requirement | Task |
| --- | --- |
| TREND after SUFFIX, before PRESENT | 2 |
| Background once per unit / invalid page | 3 |
| Linear min/max Y, four labels | 1, 3 |
| Incremental columns, 1px green, 3px erase | 4 |
| Grid restore once per pass | 4 |
| Unit change invalidates both pages | 2, 3 |
| Ignore 1/2/5 axis | 1, 4 |
| Reading fail aborts frame; trend fail still presents | 5 |
| Host tests for linear map + unit clear | 1 |
| On-target 30Hz steady state | 6 |
| No dual-page write / whole-page BTE | all |
| Sync firmware/src and CubeMX headers | 1 |
