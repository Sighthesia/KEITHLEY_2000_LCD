# ADR-0004: 顶两行信息栏重设计（绿 Badge / 去重 / 单元格 / TRIGGER 圆点）

- 状态：已接受（2026-09-03，用户确认：Badge 也变绿；行1去重后全保留；TRIG 激活常显+圆点闪）
- 前置：ADR-0003（INFO 阶段只写 y24..51，禁屏底线）继续有效。

## 决策

1. 颜色：Badge 底与 InfoBar 下沿分隔线统一绿色（`BADGE_BG/DIVIDER = GREEN`，黑字不变）；浅色分隔线复用网格色（`SEP = GRID`，与统计栏发丝同族）。
2. 行1（y0..23）：品牌后加 1x12 浅色半高分隔线（垂直居中），状态左对齐；行1不再显示 AUTO / FILT / REL（它们住在行2，避免重复）。
3. 行2（y24..49）：Zin / Range / Rate 用统计栏式单元格（名=BAR+muted，值=BAR_ALT），每块右侧 1px 浅色分隔线；右侧 FILT / REL / MATH 只在激活时显示（无 muted 占位）。
4. TRIGGER 块右对齐：浅色线＋`TRIGGER` 常显绿字＋ 8x8 圆点（用方块代圆，GE 无圆原语），TRIG 有效时圆点以 250ms 相位闪烁，无效时整块擦除。

## 实现要点（固件）

- 行1灯串在渲染器内由 `status_active` 现场组装（`row1_status_text()`），跳过 REL(6)/FILT(7)/AUTO(8)；逐页缓存 `s_reading_only_page_row1`，脏检查与擦除统一按"新旧并集"处理（品牌变窄不留残影）。
- 行2按块合作式绘制（3 块 x 4 步：值底→名→值→右分隔线），右灯跳过失活项且钳位在 trigger 分隔线左侧。
- 圆点闪烁走独立 250ms 相位（`update_blink` 内，不依赖光标 blink）；翻转且 TRIG 有效时置 `s_trig_dot_pending` 强制 INFO 阶段；内容命中缓存时走单 8x8 快刷，不重画整行；TRIG 通断计入 `row2_info_lamps()` 使整行重画、杜绝残留。
- 点亮属于 INFO 阶段的正常帧管线（仍走 CLEAR→…→PRESENT 全重画），不另开捷径，避免隐藏页陈旧内容被翻出。

## 影响文件

- `firmware/src/main_display.h` ↔ `KEITHLEY_2000_LCD/Core/Inc/main_display.h`（常量，逐字节同步）
- `KEITHLEY_2000_LCD/Core/Src/main.c`（行1/行2渲染器＋圆点相位＋脏位）
- `sim/index.html`（像素级同步；圆点用 250ms 定时重画）
- `sim/verify.js`（旧"borderless/active-only"断言替换为新设计断言）
