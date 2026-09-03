# ADR-0003: Badge 黄色分隔线的带归属与边界约束

- 状态：已接受（2026-09-03）
- 背景：模仿参考图实现 `DC Voltage` 黄底 Badge 时，连带画了两条黄线——InfoBar 下沿（y50）与屏幕底边（y318）。真机出现"最底部意外黄色线"。

## 根因

1. 屏幕底边黄线 `ui_fill_rect(0, UI_HEIGHT-2, 960, 2)` 落在 X 轴沟槽内：
   `X_AXIS_Y=298, H=22` → 覆盖 `298..319`，底线占据 `318..319`，与 X 轴标签行像素重叠，视觉上为一条贴边异物线。
2. 带归属越界：底线由 `READING_ONLY_INFO` 阶段（归属 InfoBar 带 y24..51）写入 TREND 带像素，打破"一阶段只写自家带"的隐藏页 band 同步假设（`FRAME_REGION_*` 按带拷贝），可引发翻页残留/闪烁。
3. 参考图本身只有 Badge 下沿一条黄线用于分隔档位行与读数区，屏底并无黄线——第二条是过度实现。

## 决策

- 只保留 InfoBar 下沿单条黄色分隔线：`(0, INFO_BAR_Y+INFO_BAR_H, 960, YELLOW_LINE_H)`，即 y50..51。
- 删除屏幕底边黄线（固件 `reading_only_render_info_panel()` idx==13 步与仿真器对应 `fillRect`）。
- 约束（以后所有 chrome/分隔线必须遵守）：
  - INFO 阶段只允许触碰 `y24..51`；TREND 带（y192..319，含 X 轴沟槽 298..319）只由 trend 背景/轴/列阶段写入；
  - 任何全宽屏边框若将来需要，必须走独立的 chrome 阶段并登记对应 band，不得搭车写在 info/trend 阶段内；
  - 贴屏边（y=0 / y=319）的绘制必须先确认 `panel_transform` 裁剪与沟槽标签无重叠。

## 影响文件

- `KEITHLEY_2000_LCD/Core/Src/main.c`：删除 idx==13 底线步，注释注明边界约束。
- `sim/index.html`：删除屏底 `fillRect`，注释同步。
- 本 ADR 为边界 bug 的永久记录；`main_display.h` 的 `YELLOW_LINE_H` 常量保留（仅顶部分隔线使用）。
