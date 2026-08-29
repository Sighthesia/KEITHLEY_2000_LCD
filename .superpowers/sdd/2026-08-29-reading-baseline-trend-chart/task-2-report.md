# Task 2 Report: 接入 TREND 阶段骨架

## 实现内容

- 在 `reading_only_stage_t` 中于 `READING_ONLY_SUFFIX` 与
  `READING_ONLY_PRESENT` 之间加入 `READING_ONLY_TREND`。
- 按任务简报加入双页 TREND 背景有效标志、趋势单位缓存、Y 轴标签缓存、
  趋势列游标和背景失败标志。
- `SUFFIX` 成功路径现在重置趋势列游标和背景失败状态，然后进入
  `READING_ONLY_TREND`。
- `READING_ONLY_TREND` 当前只调用 `main_display_format_trend()`，在有趋势
  数据时调用 `main_display_format_linear_trend_labels()`，并通过
  `trend_buffer_display_unit(&s_trend)` 检测单位变化。
- 趋势单位变化时同时作废两个 SDRAM 页的趋势背景标志，随后进入
  `READING_ONLY_PRESENT`。
- 未实现趋势背景、趋势列或图表绘制；未修改旧 trend draw helpers。

## 验证

执行：

```text
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

结果：成功编译并链接 `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`。

```text
RAM:   15112 B / 20 KB  (73.79%)
FLASH: 37188 B / 64 KB  (56.74%)
```

另外执行 `git diff --check`，无空白错误。

## 自审

- 阶段顺序为 `SUFFIX -> TREND -> PRESENT`。
- TREND 阶段没有引入绘图调用，符合 Task 2 范围。
- 新增的标签缓存按简报保留；由于本阶段尚未消费标签缓存，使用显式
  `(void)` 保持 `-Werror` 构建通过。
- 变更文件只有 `KEITHLEY_2000_LCD/Core/Src/main.c`。

## 提交

提交消息：`feat: add reading-only TREND stage`
