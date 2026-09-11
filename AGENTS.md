# KEITHLEY 2000 TFT 固件重建

STM32F103C8T6 的 KEITHLEY 2000 TFT 替换固件，包含真机 CubeMX 工程、离线逻辑骨架、浏览器仿真器和 LT7680 外部资源工具。

## 先看哪里

- `KEITHLEY_2000_LCD/`：唯一的真机固件和烧录来源；目标是 STM32F103C8T6。
- `firmware/src/`：纯逻辑镜像源；对应文件必须与 `KEITHLEY_2000_LCD/Core/{Src,Inc}/` 保持一致。
- `sim/`：960x320 单文件布局仿真器；`sim/font_data.js` 是生成文件，不要手改。
- `tools/`：RIF 资源镜像打包、校验和宿主测试；生成的 `.img` 只放在被忽略的 `build/` 下。
- `docs/`：网表、协议观测、ADR 和验收计划；确定性网表优先于旧网表或 ODS 记录。
- `Firmware STM32_K2000 DisplayBoard TFT_V16/`：闭源上游镜像/资料，不是本工程源码。

## 常用验证

从仓库根目录执行：

```sh
cd firmware && ./tests/run_tests.sh
cd firmware && make
node sim/verify.js
tools/tests/run_tests.sh
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

- `firmware/tests/run_tests.sh` 使用宿主 `gcc`；新增 `firmware/src/*.c` 后要同步维护脚本中的 `SRCS`。
- `firmware/Makefile` 是离线裸机检查，与 CubeMX 构建无关，需要 `arm-none-eabi-gcc`。
- 真机 CMake 使用仓库内的 `KEITHLEY_2000_LCD/build/{Debug,Release}`；不要使用顶层历史 `build/` 树。可用 `cmake --preset Release` 首次配置。
- 烧录前至少通过宿主测试、`node sim/verify.js` 和 Release ELF 构建；发布/烧录 ELF 只能使用 `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`。

## 必须保持的边界

- 修改共享逻辑时，先改 `firmware/src/`，再手工同步到 CubeMX 树，并用 `diff -q` 检查；硬件适配文件（HAL、`main.c`、LT7680 驱动）两树允许不同。
- 面板 UART 是主机共享的二进制线：默认 `K2000_UART_LOG=0`、`K2000_PERF_LOG=0`，禁止未门控的文本或裸数字发送。协议外输出会被主机当作按键。
- `K2000_DEMO_FEED` 默认必须为 `0`；仅无主机验收时启用，验收后恢复为 `0` 并重编译。
- 主机链路为 `9600 8N1`；`0x0F` 身份查询必须在 RX 中断路径即时回复。具体协议和渲染约束见相关 skill，不要从旧 ODS 推断完整规范。
- 共享 `PA3` 同时复位 LT7680 和 LCD：启动先保持复位/消隐，且必须 `lt7680_reset()` 后再 `hal_panel_init()`。
- 最新硬件引脚以 `docs/KEITHLEY2000_2026-08-08.tel` 和 `docs/Netlist_Schematic1_2026-09-08.tel` 为准；键盘行列是 `PB0..PB3` / `PB4..PB11`，不要恢复旧的 `PA0..PA3` 假设。
- RIF 资源 Flash 挂在 LT7680，不经 STM32；写入前备份原片、确认 `--base-offset`，先用 `tools/verify_resource_flash.py` 校验，禁止提交生成的 `.img`。

## 烧录和硬件调试

- 本仓库的 `openocd.cfg` 当前是 CMSIS-DAP 配置，使用 `reset_config srst_only srst_nogate`、`WORKAREASIZE 0x100`、`adapter speed 8000`；烧录采用 `init` → `halt` → `program ... verify` → `reset` → `shutdown`，不要使用 `program ... reset exit`。
- 烧录命令：

  ```sh
  openocd -f openocd.cfg -c "init" -c "halt" \
    -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" \
    -c "reset" -c "shutdown"
  ```

- 只在限流供电且断开仪器高压测量路径后做硬件验证；保留原始 V15/V16 镜像。OpenOCD、LT7680 初始化和渲染排障先加载对应 skill。

## 本地 Skill

只在任务匹配时加载详细说明：

| Skill | 触发场景 |
| --- | --- |
| [lt7680-st7701](.agents/skills/lt7680-st7701/SKILL.md) | LT7680/ST7701 初始化、黑屏、时序、共享复位 |
| [lt7680-render-coherence](.agents/skills/lt7680-render-coherence/SKILL.md) | 双页渲染、趋势、闪烁、卡顿、提交活锁 |
| [openocd-stm32-flash](.agents/skills/openocd-stm32-flash/SKILL.md) | STM32 烧录、复位、SWD 连接失败 |
| [gcc](.agents/skills/gcc/SKILL.md) | CMake、Ninja、arm-none-eabi 构建和大小分析 |
| [embedded-systems](.agents/skills/embedded-systems/SKILL.md) | STM32 外设、裸机、实时性或功耗 |
| [arm-cortex-expert](.agents/skills/arm-cortex-expert/SKILL.md) | Cortex-M 驱动、ISR、底层固件设计 |
| [embedded-debug](.agents/skills/embedded-debug/SKILL.md) | HardFault、崩溃、寄存器或栈分析 |
| [memory-audit](.agents/skills/memory-audit/SKILL.md) | Flash/RAM、栈、静态分配和内存风险 |
| [timing-analysis](.agents/skills/timing-analysis/SKILL.md) | ISR 延迟、DMA、SPI/UART 时序和帧率 |
| [serial](.agents/skills/serial/SKILL.md) | 串口扫描、抓取、Hex 和协议联调 |
| [openocd](.agents/skills/openocd/SKILL.md) | OpenOCD 通用调试和内存采样 |
| [eide](.agents/skills/eide/SKILL.md) | EIDE 工程或 `eide.yml` |
| [keil](.agents/skills/keil/SKILL.md) | Keil/MDK 工程 |
| [probe-rs](.agents/skills/probe-rs/SKILL.md) | probe-rs、CMSIS-DAP 或 RTT |
| [jlink](.agents/skills/jlink/SKILL.md) | J-Link 调试或烧录 |
| [workflow](.agents/skills/workflow/SKILL.md) | 明确请求一键 build/flash/debug/observe |
| [can](.agents/skills/can/SKILL.md) | CAN/CAN-FD 总线操作 |
| [net](.agents/skills/net/SKILL.md) | 网络抓包或连通性调试 |
| [terminal](.agents/skills/terminal/SKILL.md) | 需要持续交互的终端/串口会话 |
| [ssh](.agents/skills/ssh/SKILL.md) | SSH 远程操作 |

上游素材受根目录 `LICENSE.md` 约束；不要发布闭源固件反编译代码或未经许可的改编内容。
