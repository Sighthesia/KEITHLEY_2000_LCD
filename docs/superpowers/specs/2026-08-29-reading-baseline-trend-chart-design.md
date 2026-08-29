# Reading-Baseline Trend Chart

Date: 2026-08-29
Status: Approved in design review

## Goal

在已稳定的 500Hz 输入 / 30Hz 主读数基线上接入趋势图：静态背景只在单位切换时建一次，之后每帧只增量更新变化的曲线列。主读数、状态栏、信息栏路径保持不变。

## Scope

- 保留 `K2000_READING_ONLY_BASELINE`、隐藏页合成、状态栏/信息栏外部小字 DMA。
- 复用现有 `trend_buffer`（500 个 20ms 桶、10s 窗、240 列投影）和 `main_display` 布局常量。
- 在 `reading_only_render()` 状态机里于读数之后、提交之前增加趋势阶段。
- 不恢复旧全场景调度、双页同时写、整页/整带 BTE 拷贝。
- 不启用 `trend_axis.c` 的 1/2/5 驻留轴与候选超时。本版 Y 映射始终用当前窗口 min/max 线性压缩。
- `main_display_format_trend()` 仍输出 1/2/5 字段，供既有宿主测试；读数基线路径忽略 `trend_axis_step/top`，Y 标签由该路径按窗口 min/max 本地生成。
- 不做发光描边、圆角、分页点；不改 RIF / 大字 / 状态栏 DMA。

## Product Decisions

| Topic | Decision |
| --- | --- |
| Completeness | 面板背景 + 网格 + L 形轴槽 + 轴标签 + 滚动曲线一次做齐 |
| Background | 单位身份切换时清背景并全量重建；同单位不重填面板 |
| Curve update | 与 30Hz 读数同帧增量：只擦/画相对该页上次投影有变化的列 |
| Y mapping | 当前窗口 min/max 线性压进绘图区；四点 Y 标签按该范围均分 |
| Flat signal | min==max 时用该值幅度的一小段非零跨度，曲线画在垂直中线 |
| X axis | 固定 `10.00s 7.50s 5.00s 2.50s 0.00s`，最新点在右 |
| Unit change | `trend_buffer` 已按完整显示单位身份清空；两页趋势背景均标无效 |
| 1/2/5 axis | 本版不调用、不作为曲线投影依据 |
| Failure | 读数失败丢整帧；趋势失败仍提交已画完的读数，趋势下帧续 |

## Layout

沿用 `main_display.h`，逻辑空间 960×320：

```text
y=0..23      状态栏（已接入）
y=24..191    主读数 + 右侧信息栏（已接入）
y=192..319   趋势区
  x0..95     Y 轴槽（COLOR_BAR），标签右对齐距绘图区 4px
  x96..935   绘图区 840×92（y196..287，黑底）+ 底部分隔
  y296..319  X 轴槽，标签按宽度居中并 clamp
```

曲线不得画进读数带或信息栏。

## Scheduling

- 输入 `K2000_DEMO_INPUT_HZ=500`；显示 `DISPLAY_FRAME_PERIOD_MS=33`。
- 有效读数继续写入 `trend_buffer`；无效文本（`OVERFLOW` / `----`）不采样。
- 趋势不另开节拍。`reading_only_render()` 在 `SUFFIX` 完成后进入 `TREND`，再 `PRESENT`。
- 主读数失败：整帧丢弃，最新快照下一周期再画，趋势不提交。
- 趋势失败：隐藏页上的读数/状态/信息仍提交；该页趋势背景或列缓存保持 dirty，下帧续画。
- 背景重建中途失败：该页背景标记无效，下帧重来，半成品不得当作有效背景。

## Rendering

每帧顺序（隐藏页）：

1. 选页（与现基线相同的 page flip）
2. 状态栏 / 信息栏（仅内容变化）
3. 主读数（值 / 单位 / 半高后缀）
4. 趋势
5. `present_page` 原子提交

### 静态背景（每页独立 valid 标志）

单位身份变化或该页背景无效时：

1. 填趋势面板 `COLOR_BAR`
2. 填绘图区黑底
3. 顶部分隔 `COLOR_BAR_ALT`
4. 画网格（4 条水平、5 条垂直，`COLOR_GRID`）
5. 画 X 标签（固定五档）和当前 Y 标签（`COLOR_CYAN`，12×24 小字 DMA）

同单位且背景已 valid：跳过 1–4。

### 曲线

- `trend_buffer_project()` → 240 列 min/max。
- 列 x：`PLOT_X + column * PLOT_W / 240`。中心画 1px 霓虹绿竖段（该列 min→max）；擦除宽度 3px。
- Y：`y = PLOT_Y + (max - v) * PLOT_H / (max - min)`，夹紧在绘图区内。
- 每页保存上次 `y0/y1/occupied`。未变化列跳过。
- 擦到网格线则本帧末尾对该页补一次网格，不逐列补。
- 空窗不画假线。窗口未满时曲线从右生长，满窗后向左滚出。

### Y 标签

四个标签对应绘图区顶、2/3、1/3、底，数值为显示单位下的 `maximum, 2/3, 1/3, minimum`。由 `reading_only` 路径按 `trend_minimum/trend_maximum` 与 `trend_buffer_display_scale()` 本地格式化，不采用 `format_trend` 的 1/2/5 字符串。标签文字变化才重画 Y 槽内文字（先填轴槽再写标签）。不因此重填绘图区背景。

## Data Flow

```text
500Hz demo/host reading
  -> ui_model（主读数）
  -> trend_buffer_add（单位身份变则 reset）
reading_only TREND stage
  -> main_display_format_trend 取窗口 min/max 与标签
  -> 本版忽略 frame.trend_axis_step/top，用 min/max 投影
  -> 差分列绘制
  -> PRESENT
```

`firmware/src/` 与 CubeMX 树的纯逻辑文件仍逐字节同步。趋势绘制只存在于 CubeMX `main.c`。

## Testing

离线：

- 现有 `trend_buffer` / `main_display` / `trend_axis` 宿主测试保持通过。
- 新增：同单位 min/max 变化时，列 y 按窗口范围线性压缩，不要求背景重建。
- 新增：单位身份变化清空缓冲，并要求全量背景重建。
- `node sim/verify.js` 继续核对趋势几何常量。

仿真器：

- `sim/index.html` 趋势区与固件同一套常量；Demo 开时 240 点环形滚动。
- 本版不改仿真视觉语言。

真机：

- 烧录 `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`。
- 串口 `/dev/ttyACM1`。
- 稳态定义：趋势窗口已满，且该秒内没有单位身份切换。
- 稳态一秒钟窗口：`input_hz >= 450`、`reading_frames >= 28`、`display_commits >= 28`、`missed=0`、`reading_errors=0`。
- 单位切换与随后的填窗期允许低于 30Hz；窗口满后必须回到稳态。
- 同档位三角波、窗口已满：曲线连续、背景不再全量重填；Y 标签随 min/max 更新。
- 趋势不得覆盖主读数或信息栏。

## Out of Scope

- 旧 `reading_scene_render` 全场景路径重新作为默认。
- 1/2/5 工程刻度驻留轴、候选 2s 超时换轴。
- 趋势独立 5Hz 合成器。
- 整页 BTE copy、双页穿透写。
- 时间显示、发光描边、分页点。
