# KEITHLEY 2000 TFT 显示板固件（已归档）

> **项目状态：已放弃，仅供参考。** 当前 UI 为简化调试之前的正式版本。
> LT7680 不适合绘制本项目这种复杂 UI，实测中出现大量难以根治的显示问题，
> 因此停止继续投入。请勿将此作为量产固件使用。

## 来源

- 本项目基于 [RM-Engineering 的开源硬件项目](https://github.com/RM-Engineering/KEITHLEY-2000-replacing-the-VFD-with-a-TFT-color-display)
  （Keithley 2000 VFD 改 TFT 彩屏，320×960 + LT7680 图形加速方案）修改而来。
  感谢 RM-Engineering 的开源工作，硬件选型与转换思路均参考该项目。
- 本仓库的固件、仿真器与资源工具均为我们自行开发，
  包括读数/趋势正式 UI、浏览器仿真器与 RIF 资源镜像工具链。

## 为什么放弃

- LT7680 在本板上的复杂 UI 路径（大字体 DMA/BTE、双页提交、趋势图叠加）
  长期存在黑块、花屏与扫描状黑闪，逐项修复后仍无法稳定消除。
- 诊断确认：改用内部字库、绕开外部字体 BIN 后花屏消失，
  但扫描状黑闪依然存在，问题收敛于显示扫描与 Canvas 写入竞争、
  GE 完成时序等控制器层面行为，继续投入产出比过低。
- 结论：LT7680 不适合承载这种复杂程度的 UI。如需复刻，
  建议换用算力与显存架构更合适的方案（例如 ESP32 + IPS 屏）。

## 仓库内容（参考用）

- `KEITHLEY_2000_LCD/`：真机固件（STM32F103C8T6 + LT7680 + ST7701S），唯一的烧录来源。
- `firmware/src/`：纯逻辑镜像源，与真机树对应文件保持一致。
- `sim/`：960×320 布局仿真器（`sim/font_data.js` 为生成文件）。
- `tools/`：RIF 资源镜像打包、校验与宿主测试。
- `docs/`：网表、协议观测、ADR 与验收记录。
- `.agents/skills/`：LT7680（初始化、渲染、时序）与吉时利 2000
  （协议、显示、调试）的参考资料与排障笔记，仅供参考。

## 验证（归档时通过）

```sh
cd firmware && ./tests/run_tests.sh   # 宿主测试，29 项通过
node sim/verify.js                    # 仿真校验通过
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

详细开发约束与烧录流程见 `AGENTS.md`（保留作为历史记录）。
