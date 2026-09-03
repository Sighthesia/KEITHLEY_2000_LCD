# ADR-0006: 底部只保留趋势绘图区

- 状态：已接受（2026-09-03）
- 背景：底部趋势区元素拥挤（轴沟槽、网格、轴标签、统计栏、分隔条、内框），且其中多层背景在小屏上显乱。

## 决策

- 底部 y192..319 整条变为绘图区：`PLOT_X=0`、`PLOT_Y=192`、`PLOT_W=960`、`PLOT_H=128`，全幅黑底＋绿色 sweep 曲线＋末端圆点。
- 删除：Y/X 轴标签与沟槽、网格线（含擦除时的网格回补）、统计栏（Range/MAX/MIN/AVG）、分隔条、内框、发丝线。
- 轴刻度逻辑（`keithley_trend_axis_range`＋resident 缩放）保留——sweep 仍需它做垂直投影，只是结果不再绘制为标签。
- sweep 擦除/回卷只做黑底填充，不再回补网格；逐槽像素仍双页写（闪烁教训不变）。
- 布局遗留宏（`DIVIDER/BG/AXIS/STATS/X_LABEL`）保留供休眠 legacy 渲染器编译与 `verify` 几何兼容，不再参与 baseline 绘制。

## 实现

- `firmware/src/main_display.h` ↔ `KEITHLEY_2000_LCD/Core/Inc/main_display.h`（PLOT 几何，逐字节同步）
- `KEITHLEY_2000_LCD/Core/Src/main.c`：背景函数收敛为单黑底；删除 stats 函数、标签匹配、横向网格回补；TREND 阶段去掉标签/统计分支；`trend_y_label_y`/`trend_x_label_x` 转 legacy 保留
- `sim/index.html`（整条黑底＋曲线＋圆点）、`sim/verify.js`（plot-only 断言）
- 副作用：FLASH 75.53%→72.30%，RAM 84.57%→83.44%
