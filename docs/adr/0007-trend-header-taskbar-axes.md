# ADR-0007: 趋势区 header / 任务栏 / 纵轴

- 状态：已接受（2026-09-03）
- 前置：ADR-0006（底部只留绘图区）被本决策部分取代——绘图区让出 header / 任务栏 / Y 沟槽。

## 布局（底部 y192..319）

- header（y192 h26，BAR 底）：左侧 `Trend` 白字；右侧依次 `MAX v` / `AVG v` / `MIN v`（白）＋档位范围（绿， far right）。
- 绘图区（x96..960，y218..296，黑底）：5 条纵指示线（GRID），无横线、无标签悬浮。
- 任务栏（y296 h24，BAR 底）：纵线下方流逝时间（`10s 7.5s 5s 2.5s 0s`，青色），`0s` 在最新数据点（最右）。
- 左沟槽（x0..96，BAR 底）：resident 轴的最大/中/最小标签（青色右对齐）。

## 数据来源

- 档位范围由 resident 轴推导（`s_trend_axis_min/max/unit`），如 `±10V` / `100kΩ`（k/M 进制、DC/AC 后缀剥离、手动两位小数，无 float printf）；无数据时 `--`。
- MAX/AVG/MIN 沿用 `main_display_format_trend()` 的窗口统计文本；无数据时 `--`。
- 纵轴标签沿用 `main_display_format_linear_trend_labels()`（投影后的 resident 跨度）。
- 流逝时间由 `TREND_WINDOW_MS`（10s）五等分推导，与 sweep 槽位同源，刷新（背景重建）后重画。

## 渲染约束

- header 右块每串独占一步（单任务规则）；右对齐累计只在串之间垫间隔。
- 槽位擦除回补被盖住的纵指示线段（`trend_sweep_restore_verticals`）；回卷清屏后重画 5 线。
- Y 标签只在背景重建时绘制（同单位漂移不触发重建，标签即 resident 身份，天然稳定）。
- 空窗显示居中 `WAITING FOR DATA`；首个样本（单位变化）触发背景重建。

## 后续调整：Trend 徽标＋绘图顶端绿线

- `Trend` 改为仿档位徽标（绿底黑字，80px 宽 Badge，与行2档位标签同语言），不再是 header 上的白字。
- 绘图区顶端加全幅 2px 绿线（与行2分隔线同高同色）；纵指示线、顶部 Y 标签、擦除回补、回卷重画一律从线下起算，互不覆盖。

## 后续调整：绿线改 Trend 顶边＋统计抖动根治

- 绿线从绘图顶端搬到 Trend 顶边（y192 全幅 2px）；徽标与 header 文字整体下移 2px（线下内容带 y194..218），纵指示线/Y 标签/擦除回补回到绘图区基准。
- 统计抖动根因：两页在相邻帧各做一次背景重建，窗口滑动使 MAX/AVG/MIN 末位不同→翻页交替闪烁（与 sweep 双页教训同类）。
- 修复：header/任务栏/沟槽/标签/纵线复用 sweep 门控双页同写；提交时兄弟页共享有效位＋单位（其绘图区像素与记账不动，经 sweep 重扫收敛）。

## 影响文件

- `firmware/src/main_display.h` ↔ `KEITHLEY_2000_LCD/Core/Inc/main_display.h`（三区几何）
- `KEITHLEY_2000_LCD/Core/Src/main.c`（helper＋背景17步＋stage标签＋擦除回补）
- `sim/index.html`、`sim/verify.js`
