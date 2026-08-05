# KEITHLEY 2000 TFT 固件重建

本目录是 KEITHLEY 2000 VFD 替换为 TFT 的硬件/闭源固件资料集，不是现成的可编译 STM32 工程。

## 资料边界

- 当前只有带元件型号的网表、固件镜像、变更日志和协议表格；没有 C/C++ 源码、工程文件、构建脚本、测试或 CI。
- `Firmware STM32_K2000 DisplayBoard TFT_V16/Firmware STM32_K2000 DisplayBoard TFT_V16.hex` 是 V16 Intel HEX，装载地址从 `0x08000000` 开始；`Flash Data_K2000 Display Board TFT_V15.bin` 是 200704 字节的 V15 Flash 数据。不要把两者当作同一镜像或直接互换烧录。
- `K2000_Panel_Protocol.ods` 现在是有效的 OpenDocument 表格，包含 `TX`、`RX`、`Notes` 三张表；协议表是观测/实现记录，不等于完整的 KEITHLEY 主机协议规范。
- `Netlist_K2000_Display Board_TFT V20.txt` 明确列出 `IC2=STM32F103C8`、`IC3=LT7680A-R`、`IC4=W25Q128JV`、`IC5=AP3031/AP3032`，但仍不是完整原理图；器件封装、未列出的网络和版本差异必须回到上游原理图/BOM核实。

## 分析与重建

- 先以带型号网表建立硬件抽象和引脚表，再分析固件镜像；当前可采用 `STM32F103C8`、`LT7680A-R`、`W25Q128JV` 作为分析起点，但仍需实物/原理图验证时钟、封装和工作模式。
- 网表确认：`PA9/PA10` 为经 `BSS138` 电平转换的 `USART_TX/RX`；`PB12..PB15` 为 `SPI2_SS/SCK/MISO/MOSI`；`PA0=LCD_CS`、`PA1=LCD_SCLK`、`PA2=LCD_SDI`、`PA3=LCM_RES`、`PA4=LCM_SS`、`PA5=LCM_SCK`、`PA6=LCM_SDO`、`PA7=LCM_INT`；`PB0..PB7` 连接 `KEY_COL1..8`，`PC13/PC14/PC15/PB10` 连接 `KEY_ROW1..4`；LT7680 通过 `FLASH_*` 连接 `W25Q128JV`，并使用缓冲后的 `LCM_SCK/LCM_SDO` 与面板 `X6` 的 RGB/控制信号。
- 旧版 ODS 里的 `TX` 行选记录可以作为历史参考，但和最新网表相比已经过时；实现键盘扫描时以最新网表为准，不要再按 `PA0..PA3` 写死。
- ODS 的 `RX`/`Notes` 记录了 `TAG + value`、`0x0D` 消息起始、显示标签、光标定位 `POS`、闪烁和 VFD 指示器字段；这些可作为协议逆向线索，不能据此断言所有消息边界、未实现按键或标签含义。
- 任何新固件先做离线构建和静态检查，再在限流电源、断开仪器高压测量路径的条件下验证；首次烧录保留原始 V15/V16 镜像和可恢复的 SWD 接线。
- 目前没有可执行的 build/test 命令。建立 STM32 工程后，必须把确切工具链、目标 MCU、链接脚本、烧录命令和最小硬件验收步骤补充到本文件。
- 可用 `arm-none-eabi-objcopy -I ihex -O binary <firmware.hex> <firmware.bin>` 将 HEX 转为二进制供静态分析；转换不会恢复源代码或协议语义。
- `firmware/src/` 已建立驱动骨架（`lt7680_bus`、`lt7680_gfx`、`k2000_proto`、`ui_model`、HAL stub、示例 `main.c`），可在 `firmware/` 下用 `make` 离线编译验证，使用 `arm-none-eabi-gcc`（`-mcpu=cortex-m3 -mthumb`）。当前链接为示例/裸机骨架，尚未包含真实 STM32 启动代码、链接脚本、外设寄存器驱动和烧录流程；`lt7680_gfx.c` 中的寄存器地址为占位符，需按 LT7680A-R 数据手册核对。

## 版本与许可

- `Change Log V16.txt` 只记录一项 V15 变更：图形加速控制板 Reset 线改为开漏；不要据此假定完整版本差异。ODS 的 `TX` 表还标记部分按键为 `Confirmed`、`Char unknown` 或 `Not wired`，这些状态必须保留在协议/键盘分析记录中。
- 上游 `LICENSE.md` 是 CC BY-NC-ND 4.0。共享上游材料需保留署名和许可信息；不要发布基于上游材料的改编版本或闭源固件的反编译代码，许可边界有疑问时先停下核实。

## 目录

- `Firmware STM32_K2000 DisplayBoard TFT_V16/`：V16 HEX 与变更日志。
- 根目录：V15 Flash 数据、V20 带型号网表、面板协议 ODS。
