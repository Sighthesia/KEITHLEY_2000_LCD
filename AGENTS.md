# KEITHLEY 2000 TFT 固件重建

本目录是 KEITHLEY 2000 VFD 替换为 TFT 的硬件/闭源固件资料集，不是现成的可编译 STM32 工程。

## 资料边界

- 当前只有带元件型号的网表、固件镜像、变更日志和协议表格；没有 C/C++ 源码、工程文件、构建脚本、测试或 CI。
- `Firmware STM32_K2000 DisplayBoard TFT_V16/Firmware STM32_K2000 DisplayBoard TFT_V16.hex` 是 V16 Intel HEX，装载地址从 `0x08000000` 开始；`Flash Data_K2000 Display Board TFT_V15.bin` 是 200704 字节的 V15 Flash 数据。不要把两者当作同一镜像或直接互换烧录。
- `K2000_Panel_Protocol.ods` 现在是有效的 OpenDocument 表格，包含 `TX`、`RX`、`Notes` 三张表；协议表是观测/实现记录，不等于完整的 KEITHLEY 主机协议规范。
- `Netlist_K2000_Display Board_TFT V20.txt` 明确列出 `IC2=STM32F103C8`、`IC3=LT7680A-R`、`IC4=W25Q128JV`、`IC5=AP3031/AP3032`，但仍不是完整原理图；器件封装、未列出的网络和版本差异必须回到上游原理图/BOM核实。更新更全的信号网表见 `KEITHLEY2000_2026-08-08.tel`（组件位号 U1=STM32F103C8T6、U2=LT7680A-R 10MHz、U5=W25Q128JVSIQ、U3/U4/U6=BAT54、Q1=BSS138）。

## 分析与重建

- 先以带型号网表建立硬件抽象和引脚表，再分析固件镜像；当前可采用 `STM32F103C8`、`LT7680A-R`、`W25Q128JV` 作为分析起点，但仍需实物/原理图验证时钟、封装和工作模式。
- **引脚表以 `KEITHLEY2000_2026-08-08.tel`（确定性网表）为准**：`LCM_SS`=PA4↔LT7680 SCS、`LCM_SCK`=PA5↔LT7680 SCK、`LCM_SDI`=PA7↔LT7680 SDI、`LCM_SDO`=PA6↔LT7680 SDO、`LCM_INT`=PA8↔LT7680 INT、`LCM_RES`=PA3 经 BAT54 分路到 LT7680 RST 与 LCD RES；`LCD_CS/LCD_SCLK/LCD_SDI`=PA0/PA1/PA2 只连面板（9-bit SPI）；`PA9/PA10` 为经 BSS138/电阻电平转换的 `UART_TX/RX`（TXB/RXB 5V 侧）；`FLASH_*`（U2.20-23）连 `W25Q128JV`（U5），不经 STM32；LT7680 晶振 10MHz。固件 `hal_board.c` 引脚定义与该网表一致。
- 旧 `Netlist_K2000_Display Board_TFT V20.txt` 已按 TEL 修正（`docs/` 下为 2026-08-08 修正版），芯片级映射一律以 TEL 为准：LT7680 SCS=PA4（不是 PA0）、`LCM_INT`=PA8（不是 PA7）、`LCM_SDI`=PA7、PA3 经 BAT54 分路到 LT7680 RST 与 LCD RES；数据线引脚号按 QFN-68 修正。`V20` 仍可用于键盘（`PB0..PB7`=`KEY_COL1..8`、`PC13/PC14/PC15/PB10`=`KEY_ROW1..4`）与面板 X6 引脚号参考。
- 旧版 ODS 里的 `TX` 行选记录可以作为历史参考，但和最新网表相比已经过时；实现键盘扫描时以最新网表为准，不要再按 `PA0..PA3` 写死。
- ODS 的 `RX`/`Notes` 记录了 `TAG + value`、`0x0D` 消息起始、显示标签、光标定位 `POS`、闪烁和 VFD 指示器字段；这些可作为协议逆向线索，不能据此断言所有消息边界、未实现按键或标签含义。
- 任何新固件先做离线构建和静态检查，再在限流电源、断开仪器高压测量路径的条件下验证；首次烧录保留原始 V15/V16 镜像和可恢复的 SWD 接线。

## 已验证的构建/烧录/验收流程（2026-08-08 彩条成功）

- 工程：`KEITHLEY_2000_LCD/`（STM32CubeMX + CMake/Ninja，`firmware/` 为离线骨架，两者 lt7680 驱动保持同步）。目标 MCU `STM32F103C8T6`，链接脚本见 `KEITHLEY_2000_LCD/STM32F103C8TX_FLASH.ld`。
- 构建：`cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`（生成 `.elf`，烧录用它）。
- 烧录（ST-Link V2 SWD，`openocd.cfg` 为 `reset_config none`，务必用 halt 先行序列，不要用 `program ... reset exit`）：
  ```
  openocd -f openocd.cfg \
    -c "init" -c "halt" \
    -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" \
    -c "reset" -c "shutdown"
  ```
  若最后 reset 报错，写入其实已成功，按板上复位键启动即可；软件复位不可靠时手动按复位。
- 硬件验收（彩条里程碑，已通过）：`LT7680_SPI_SELFTEST=0` 正常路径 → `lt7680_reset()`（PA3 同时复位 LT7680 与面板）→ `hal_panel_init()`（9-bit SPI ST7701S 序列，`0x11`→`0x35 0x00`→`0x3A 0x66`→`0x29`）→ `lt7680_gfx_init()` → `lt7680_gfx_show_color_bars()`；115200 串口回 `STATUS=0x..` / `PASS color-bars enabled`，屏幕显示彩条。
- **关键教训：`hal_panel_init()` 必须在 `lt7680_reset()` 之后调用**。PA3 经 BAT54 分路到 LT7680 RST 与 LCD RES 两条复位线；先初始化面板再复位会把刚写进面板的配置抹掉（这是黑屏根因之一）。
- LT7680A-R + ST7701S 的初始化细节（SPI 协议、PLL/SDRAM/时序寄存器、已验证的 V16 寄存器值、彩条验收、黑屏排查顺序）见 skill `.agents/skills/lt7680-st7701/SKILL.md`；烧录用 `.agents/skills/openocd-stm32-flash/SKILL.md`。
- 可用 `arm-none-eabi-objcopy -I ihex -O binary <firmware.hex> <firmware.bin>` 将 HEX 转为二进制供静态分析；转换不会恢复源代码或协议语义。
- `firmware/src/` 已建立驱动骨架（`lt7680_bus`、`lt7680_gfx`、`k2000_proto`、`ui_model`、HAL stub、示例 `main.c`），可在 `firmware/` 下用 `make` 离线编译验证，使用 `arm-none-eabi-gcc`（`-mcpu=cortex-m3 -mthumb`）。当前链接为示例/裸机骨架，尚未包含真实 STM32 启动代码、链接脚本、外设寄存器驱动和烧录流程；`lt7680_gfx.c` 中的寄存器地址为占位符，需按 LT7680A-R 数据手册核对。

## 版本与许可

- `Change Log V16.txt` 只记录一项 V15 变更：图形加速控制板 Reset 线改为开漏；不要据此假定完整版本差异。ODS 的 `TX` 表还标记部分按键为 `Confirmed`、`Char unknown` 或 `Not wired`，这些状态必须保留在协议/键盘分析记录中。
- 上游 `LICENSE.md` 是 CC BY-NC-ND 4.0。共享上游材料需保留署名和许可信息；不要发布基于上游材料的改编版本或闭源固件的反编译代码，许可边界有疑问时先停下核实。

## 目录

- `Firmware STM32_K2000 DisplayBoard TFT_V16/`：V16 HEX 与变更日志。
- 根目录：V15 Flash 数据、面板协议 ODS。
- `docs/`：`KEITHLEY2000_2026-08-08.tel`（确定性网表，权威）、`Netlist_K2000_Display Board_TFT V20.txt`（2026-08-08 已按 TEL 修正版）、`K2000_Panel_Protocol.ods`、黑屏分诊与示波器测量笔记。
