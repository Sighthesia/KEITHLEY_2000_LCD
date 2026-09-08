# KEITHLEY 2000 TFT 固件重建

本目录是 KEITHLEY 2000 VFD 替换为 TFT 的硬件/闭源固件资料集。上游只提供网表、固件镜像、变更日志和协议表格（无源码）；仓库内另有**自研的可编译 STM32 工程、离线固件骨架、HTML 布局仿真器与测试**，两者分层清晰。

## 资料边界（上游素材）

- 上游素材不含任何 C/C++ 源码：网表、固件镜像、变更日志、协议表格。自研部分见下方「已验证的开发流程」。
- `Firmware STM32_K2000 DisplayBoard TFT_V16/Firmware STM32_K2000 DisplayBoard TFT_V16.hex` 是 V16 Intel HEX，装载地址从 `0x08000000` 开始；`Flash Data_K2000 Display Board TFT_V15.bin` 是 200704 字节的 V15 Flash 数据。不要把两者当作同一镜像或直接互换烧录。
- `K2000_Panel_Protocol.ods` 现在是有效的 OpenDocument 表格，包含 `TX`、`RX`、`Notes` 三张表；协议表是观测/实现记录，不等于完整的 KEITHLEY 主机协议规范。
- `Netlist_K2000_Display Board_TFT V20.txt` 明确列出 `IC2=STM32F103C8`、`IC3=LT7680A-R`、`IC4=W25Q128JV`、`IC5=AP3031/AP3032`，但仍不是完整原理图；器件封装、未列出的网络和版本差异必须回到上游原理图/BOM核实。更新更全的信号网表见 `KEITHLEY2000_2026-08-08.tel`（组件位号 U1=STM32F103C8T6、U2=LT7680A-R 10MHz、U5=W25Q128JVSIQ、U3/U4/U6=BAT54、Q1=BSS138）。

## 分析与重建

- 先以带型号网表建立硬件抽象和引脚表，再分析固件镜像；当前可采用 `STM32F103C8`、`LT7680A-R`、`W25Q128JV` 作为分析起点，但仍需实物/原理图验证时钟、封装和工作模式。
- **引脚表以 `KEITHLEY2000_2026-08-08.tel`（确定性网表）为准**：`LCM_SS`=PA4↔LT7680 SCS、`LCM_SCK`=PA5↔LT7680 SCK、`LCM_SDI`=PA7↔LT7680 SDI、`LCM_SDO`=PA6↔LT7680 SDO、`LCM_INT`=PA8↔LT7680 INT、`LCM_RES`=PA3 经 BAT54 分路到 LT7680 RST 与 LCD RES；`LCD_CS/LCD_SCLK/LCD_SDI`=PA0/PA1/PA2 只连面板（9-bit SPI）；`PA9/PA10` 为经 BSS138/电阻电平转换的 `UART_TX/RX`（TXB/RXB 5V 侧）；`FLASH_*`（U2.20-23）连 `W25Q128JV`（U5），不经 STM32；LT7680 晶振 10MHz。固件 `hal_board.c`（在 CubeMX 树 `KEITHLEY_2000_LCD/Core/Src/`，不在 `firmware/src/`）引脚定义与该网表一致。
- 旧 `Netlist_K2000_Display Board_TFT V20.txt` 已按 TEL 修正（`docs/` 下为 2026-08-08 修正版），芯片级映射一律以 TEL 为准：LT7680 SCS=PA4（不是 PA0）、`LCM_INT`=PA8（不是 PA7）、`LCM_SDI`=PA7、PA3 经 BAT54 分路到 LT7680 RST 与 LCD RES；数据线引脚号按 QFN-68 修正。`V20` 仍可用于面板 X6 引脚号参考。
- 键盘以原理图截图所示矩阵为准：`PB0..PB3`=`KEY_ROW1..4`（输出，空闲高、逐行拉低）、`PB4..PB11`=`KEY_COL1..8`（输入，R1..R8 33k 外部上拉，逻辑列 col0=R1=`PB11` … col7=R8=`PB4` 反序）；`PC13/PC14/PC15` 空出。旧 `PA0..PA3` 行选命名已作废，不要按它写死。
- 状态灯：`0x06 REM` 显示为 `REMOTE`；`0x07 SHIFT/REAR` 不进灯串，显示在徽标行徽标之后（白字），SHIFT 激活时分隔线之后整行变蓝（徽标区保持红徽）。功能名电阻档显示 `2W Ω`/`4W Ω`（短式，徽标区放不下全称），读数/趋势单位统一 `Ω/kΩ/MΩ`（`KOHM/kOHM` 均归一，主机发 ASCII 仍兼容识别）。
- 旧版 ODS 里的 `TX` 行选记录可以作为历史参考，但和最新网表相比已经过时；实现键盘扫描时以最新网表为准，不要再按 `PA0..PA3` 写死。
- ODS 的 `RX`/`Notes` 记录了 `TAG + value`、`0x0D` 消息起始、显示标签、光标定位 `POS`、闪烁和 VFD 指示器字段；这些可作为协议逆向线索，不能据此断言所有消息边界、未实现按键或标签含义。
- 任何新固件先做离线构建和静态检查，再在限流电源、断开仪器高压测量路径的条件下验证；首次烧录保留原始 V15/V16 镜像和可恢复的 SWD 接线。

## 已验证的构建/烧录/验收流程（2026-08-08 彩条成功）

- 工程：`KEITHLEY_2000_LCD/`（STM32CubeMX + CMake/Ninja，`firmware/` 为离线骨架，两者 lt7680 驱动保持同步）。目标 MCU `STM32F103C8T6`，链接脚本见 `KEITHLEY_2000_LCD/STM32F103C8TX_FLASH.ld`。
- 构建：`cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`（生成的 `.elf` 在 `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`，**用这个路径烧录**）。
- ⚠️ 仓库里有**两套 build 树**：工程内 `KEITHLEY_2000_LCD/build/`（权威，构建/烧录都用它）与顶层 `build/`（历史残留，ELF 布局是 `build/Release/KEITHLEY_2000_LCD/KEITHLEY_2000_LCD.elf`，**不要烧这个**，也不要执行 `cmake --build build/...`）。判断固件是否新烧：上电串口首行应为当前代码里打印的横幅。
- 烧录（ST-Link V2 SWD，`openocd.cfg` 为 `reset_config none`，务必用 halt 先行序列，不要用 `program ... reset exit`）：
  ```
  openocd -f openocd.cfg \
    -c "init" -c "halt" \
    -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" \
    -c "reset" -c "shutdown"
  ```
  若最后 reset 报错，写入其实已成功，按板上复位键启动即可；软件复位不可靠时手动按复位。
- 烧录（DAPLink/CMSIS-DAP，Horco `faed:4870`，2026-08-19 已验证）：`openocd.cfg` 为 `reset_config srst_only srst_nogate`（DAPLink 无 TRST，板上 NRST 已接）+ `set WORKAREASIZE 0x100`（廉价 CMSIS-DAP 跑不了 work-area 异步写/CRC 算法会 `timeout waiting for algorithm`，缩小 work area 强制直接写）+ `adapter speed 8000`（速度受 SWD 带宽限制，8MHz 约 9s 烧完 64KiB，10MHz 探针出错倒退到 26s）。序列用普通 `halt` 即可（固件已改为 `SWJ_NOJTAG` 保留 SWD，见下）：
  ```
  openocd -f openocd.cfg \
    -c "init" -c "halt" \
    -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" \
    -c "reset" -c "shutdown"
  ```
  细节见 skill `.agents/skills/openocd-stm32-flash/SKILL.md` 的 DAPLink 变体一节。
- **SWJ 调试保留（2026-08-19 修改）**：`stm32f1xx_hal_msp.c` 的 `HAL_MspInit()` 由 `__HAL_AFIO_REMAP_SWJ_DISABLE()` 改为 `__HAL_AFIO_REMAP_SWJ_NOJTAG()`——本板网表 PA13/14/15、PB3/4 均未占用，保留 SWD 后可随时 `init`+`halt` 连接调试（断点/单步/内存），也免去此前 `connect_assert_srst`+`reset halt` 的复位技巧；JTAG 三脚仍释放为 GPIO。若烧回仍禁用 SWJ 的旧固件，连接会再报 `cannot read IDR`，需临时加 `connect_assert_srst` 并改用 `reset halt`。
- 硬件验收（彩条里程碑，已通过）：`LT7680_SPI_SELFTEST=0` 正常路径 → `lt7680_reset()`（PA3 同时复位 LT7680 与面板）→ `hal_panel_init()`（9-bit SPI ST7701S 序列，`0x11`→`0x35 0x00`→`0x3A 0x66`→`0x29`）→ `lt7680_gfx_init()` → `lt7680_gfx_show_color_bars()`；115200 串口回 `STATUS=0x..` / `PASS color-bars enabled`，屏幕显示彩条。
- 硬件验收（数字演示里程碑，build11 已通过）：正常启动全程黑屏（复位后立即 `REG[12h]=0x08` 关显示、不调用 `show_color_bars()`），`lt7680_gfx_clear()` 用**几何引擎矩形填充**（`DCR1=REG[76h]=0xE0`，不是 MRWDP 突发），回读全部 `PX(..)=0000`，`clear-ms=5`（突发 614400 字节需 ~2s 且并不清空画布）；数字在物理屏正中、方向正确，映射是**纯转置** `fb_x=uy; fb_y=ux`（960×320 横屏 UI 空间，无任何轴反转；x 反转=上下颠倒、y 反转=左右镜像）。`lt7680_gfx_peek_pixel()` 可经 MRWDP 回读画布像素用于诊断。
- 硬件验收（趋势布局里程碑，build13 待烧录）：删除功能/参数/图表头三行，读数区扩为 y24..192（168px）；数字**左对齐**（64x128 单元，单位同尺寸并紧跟数值，电压档 `VDC/VAC` 拆出半高 `DC/AC` 用 32x64 `font_half`）；右侧右对齐信息列（Zin / Range / Rate / FILT REL MATH）。`main_display`/`render_scheduler` 已去掉三行相关 phase 与 dirty 位，Flash 62672B=95.63%（64KB 紧张，不宜再加字形）。
- 趋势坐标轴单元格（2026-08-16 增补）：趋势区使用统一 chart panel 背景（`MAIN_DISPLAY_COLOR_BAR`=#182126），黑色绘图区（`MAIN_DISPLAY_COLOR_BG`）和细分隔线；Y/X 轴仍为 L 形 gutter——Y 轴占 x0..95 整条竖带（y192..319 全高），X 轴占底部横带（x96..935，y296..319）。Y 轴标签在单元格内**右对齐**（距绘图区左缘 4px，超长 clamp 到 x=0），X 轴标签按实际宽度居中并 clamp。X 轴从左到右为 `10.00s → 0.00s`，最新采样固定在右侧。趋势序列按完整显示单位身份切换清空，Y 轴使用同单位并采用 1/2/5 工程刻度。仿真器与固件布局常量保持对应，`verify.js` 覆盖方向、轴单位和绘图区几何。CubeMX Release 当前 Flash 余量约 12B，禁止继续增加字形或静态数据。
- **演示/自测模式（无主机验收用）**：CubeMX `main.c` 的 `K2000_DEMO_FEED`（0=关，默认）编译开关，打开后主循环按 100ms 喂合成 K2000 帧走**真实管线**（`0x0D 0x01 value+unit → 0x09 → 0x08 → 0x07`），值做三角波递增，档位在 `VDC/VAC/ADC/AAC/MVDC/MVAC/MADC/MAAC/OHM/KOHM/MOHM/Hz/kHz/MHz/°CEL` 间轮换并翻 REL/FILT/AUTO/MATH、FAST/MED/SLOW、HOLD/TRIG；串口横幅带 `DEMO-FEED`。档位字母被 64x128 数字字体字符合集限制（**无 U/Z/S 字形、Flash 不足再加**），温度用 `"\xC2\xB0" "CEL"`（0x13 度符号 TAG 内联，解析器转 UTF-8 °，既能渲染又满足趋势温度识别）。demo 模式下**跳过 UART 轮询**（无主机时 RX 悬空噪声会污染解析器）。该模式 Flash 63276B=96.55%，仍能烧录；**烧录验收后务必改回 0 重编译**。仿真器 `sim/index.html` 有对应「Demo 动态档位轮换」开关，趋势图随 demo 值滚动（240 点环形），单位表须与 `s_demo_units` 保持同步。验收看三点：横幅 `DEMO-FEED`、读数 10Hz 更新、趋势柱 5Hz 滚动。
- **关键教训：`hal_panel_init()` 必须在 `lt7680_reset()` 之后调用**。PA3 经 BAT54 分路到 LT7680 RST 与 LCD RES 两条复位线；先初始化面板再复位会把刚写进面板的配置抹掉（这是黑屏根因之一）。
- **趋势图最终方案：示波器式 sweep（2026-08-30 验收通过）**。硬件滚动三原语在本板全部不可用：①PIP——按 LT768x DS V4.2 §10.3 官方流程逐寄存器配置并回读验证（含 VDIR=1 自动禁止 PIP 的冲突已用面板 MADCTL 补偿解除），PIP1/PIP2 都不合成饱和测试图案，**这颗 die 的 PIP 合成器无输出**（与 BTE 写路径同类硅缺陷）；②BTE 拷贝——花点缺陷；③主窗口地址滚动——屏级全局。sweep 设计：绝对时间桶（20ms ring）映射固定屏幕列，游标按预算推进，空槽 hold 前值（阶梯线，像素不越槽界），换档保持 epoch（**重锚 epoch 会让全部槽位横移=抖动**），满窗回卷时一次性清绘图区（`trend_sweep_wipe_cycle`），轴刻度变化触发整窗重扫。
- **关键教训（sweep 闪烁根因，画布回读实证）**：baseline 渲染是"单页写 + `hidden_page_sync_regions()` band 拷贝同步兄弟页"架构，而 sweep 从不登记 `FRAME_REGION_TREND` → 每次翻页展示的页面只有当帧新画的 2-3 个槽，曲线在"完整/几乎空白"间以提交率交替=闪烁。修复：`s_trend_sweep_drawing` 门控强制 sweep 的 GE 双页写（两页像素恒等，零 BTE）。**任何不走 region 机制的直接绘制都必须强制双页写，或接入 region 标志**。诊断手段：`sw_behind/sw_scale/sw_reset/sw_pxok` PERF 计数器 + `K2000_TREND_DUMP`（MRWDP 画布 ASCII 回读，**默认关——每帧 ~550 次慢读会卡顿，本身就是闪烁假象源**）；`sw_pxok` 对账证明像素落盘。注意 `ui_runtime_single_page()` 依赖 `s_renderer.phase`（baseline 每帧设 `UPDATE_READING`→单页生效）。
- **关键教训（demo 喂样率回归会复发）**：`K2000_DEMO_INPUT_HZ` 一旦回到 500，趋势光标 50 桶/秒风暴、单桶被 10 条读数反复撑大、槽位每帧多次重绘——症状与渲染器 bug 无法区分。已二次回归（2026-08-30 再次修回 10Hz）。凡趋势"抖动/闪烁"，先查 `input_hz` 是否为 10，再查渲染器。10Hz 下 `fps≈10` 是正确值（=读数率），旧 30fps 是 500Hz 喂样假象。
- **关键教训（2026-08-22 demo 刷新链修复）**：三个叠加缺陷导致"读数冻结、趋势空、信息栏不显示"。①`ui_draw_bitmap_slice` 的可恢复作业按参数忽略新调用（job 活跃即续绘旧串），因此**一个渲染 item 内连续多次 `ui_draw_text` 的序列**（如 FILT→REL→MATH 灯行）会在预算耗尽后每片从头重放、最后一个字符串永远画不完——item 永不前进、帧永不提交；多字符串必须写成"记住画到第几个、中断后续绘中断串"的可恢复序列。②demo 喂样周期曾被 perf 测量改成 2ms（500Hz）遗留：渲染变重后主循环靠每次补喂 8 个样本追赶，实际 ~480 样本/秒 → 档位每秒轮换约 12 次 → 趋势缓冲每秒被单位身份切换清空（图永远近乎空白）、状态内容每帧都变（重绘风暴）。必须保持 `DEMO_SAMPLE_PERIOD_MS=100`。③顶部状态栏 + 信息栏整幅重绘约需上千次双页 GE 填充（~1.7s），不能按 STATUS 脏标记逐帧重绘（每个采样都带状态 TAG）；已改为**内容变化驱动**（Zin/Range/Rate/灯组与屏上快照比较），趋势坐标轴重建加 1/16 量程滞回，小刻度漂移由增量列更新吸收。修复后稳态 fps=10、missed=0。
- **调试手段备忘**：板子保留 SWD，可用纯 OpenOCD（`halt; mdw <addr>; resume`）采样变量；gdb 批处理模式的 interrupt/detach 会把目标留在 halt 态污染现场，采样务必用 openocd -c 命令行形式并在结束时 resume。串口抓取前先 `stty -F /dev/ttyACM0 115200 raw -echo`。
- **关键教训：MRWDP 像素写的每个字节必须独立 CS 事务**（`CS低→0x80→字节→CS高`，对齐 Levetop SPI_DataWrite）。不能在单次 CS 窗口内连发 `0x80 lo 0x80 hi`：这颗 LT7680A-R 会把 0x80 之后的所有字节当数据，重复的 0x80 前缀写进像素流，画面呈现"每字节独立 RGB332 像素"的 8bpp 假象、高字节丢失。该症状与颜色深度寄存器无关（GE 填充走内部引擎不经过 SPI 数据口所以颜色正常），改 REG[02h]/[5Eh]/[10h] 均无效；2026-08-13 改每字节 CS toggle 后白条恢复纯白、GREEN/BLUE 显示正确。SPI 读回同样不可信：首字节=`0x80 XOR 低字节`、后续重复低字节，只能取 `dump[0]^0x80` 恢复低字节，颜色正确性一律以屏幕为准。
- **BTE 缓存字形渲染已验收（2026-08-21）**：RIF 大字（64x128 UI 空间）经 `lt7680_flash_read()` 读入 MCU、CPU 转置成 128x64 framebuffer tile 写入 SDRAM `0x300000+` 离屏缓存，渲染时每字形一次 `lt7680_gfx_blit()`。帧时间从 fallback ~10.5s 降到 ~0.4s。要点：①**MRWDP 回读必须先丢一个 dummy cycle**（`lt7680_gfx_peek_pixel()` 已修），否则读到错位字节（如 `07E0→E060`）；②DMA 完成判定轮询 `REG[B6h] bit0`、BTE 完成轮询 `REG[90h] bit4`，不能用共享 `CORE_BUSY`；③BTE source/destination stride 单位是像素，`DT_WTH=320`（fb 宽），cache tile 布局为转置后 128 宽×64 高；④RIF 打包前景色是霓虹绿 `#00FF33→0x07C6`，与 UI 常量 `MAIN_DISPLAY_COLOR_GREEN=0x07E6` 不同——blit 像素颜色已烘焙，门控只查背景黑；⑤离屏写缓存后必须恢复 `CVSSA+CVS_IMWTH` 两项（`select_canvas_page()` 只恢复基址不恢复宽度，漏恢复会让 GE 填充按 128 折行、画面拉伸+RGB 花点）；⑥缓存构建数据用 RIF 目录 CRC32 验收。
- **性能优化定案（2026-08-21）**：~0.4s/帧的剩余瓶颈是**每字形绘制都从 Flash 目录第 0 项逐项扫描**（每帧 ~115 次 48B 读 = 371ms，占 88%；GE 填充 3ms、BTE blit 8ms 均可忽略）。修复：预构建时把全部字形 tile 信息存入 MCU RAM 目录表 `s_dir_cache`（38 项 ≈1.2KB，RAM 94.8%），渲染时 O(1) 查表，帧内 Flash 读归零。最终 **frame-ms=12~13（预算 33ms）、fps 26~28**（demo 源 10Hz 更新，非渲染上限）。教训：LT7680 SPI FIFO 逐字节事务读 Flash 约 3ms/48B，渲染热路径上任何 Flash 扫描都是性能杀手；先测量（per-op 计时）再优化。
- **BTE 直绘字形三缺陷定案（2026-08-23，56x104 转置直绘时代）**：U5 瓦片为预转置 FB 向（104x56/stride 208），`ui_draw_external_digits` 的 BTE 门控通过后单次 `lt7680_flash_dma_tile_to_canvas` 双页直绘。三个叠加缺陷曾造成"读数摊宽+多帧叠影+横字抹进信息栏"：①**光标推进必须用 UI 栅格 `FONT_DIGIT_WIDTH`(56)，绝不能用 `tile.width`(104=转置后 FB 宽)**——用错则每格右移 48px、长读数溢出屏幕、擦除账目(56px)与实际像素(104px)永久错位成"怎么擦都擦不掉的残影"；②**DMA 失败严禁跌落 run-length 回退**——转置瓦片被回退路径当 UI 空间画成 104px 宽"横字"，且失败被静默吞掉；现改为有限重试 3 次后跳过该字形保帧提交；③字形 DMA 用全速 `SPI_DIV=0` 后**必须恢复 `DIVISOR_SAFE`**，否则后续 Flash FIFO 逐字节 CS 协议挂死（SWD 栈表现为卡死在 `flash_read→push_and_drain`）。另：`rif_init` 时校验 `'0'` 瓦片几何并打 UART 日志（BTE 模式期望 w=128? 不——按当前字体 104/56/208），不匹配即禁用外部大字退回内置字体。
- **双页写穿透与尾部擦除（2026-08-23）**：`ui_fill_rect/ui_draw_line/BTE blit` 全部双页写意味着**帧合成中途的任何擦除会立即穿透到可见页**，而补偿的新字形要等同帧稍后——中间窗口就是肉眼可见的黑洞。差分渲染的"逐格擦除消失字形"+"后缀联合矩形"因此在大字上产生随读数漂移的黑块。修复：左对齐文本的消失字形恒为连续尾段，改为**单次尾部擦除**（新内容右缘→旧内容右缘一个矩形，永远不压在待重绘字形上）；半高后缀足迹由 `s_prev_suffix/x` 记账（后缀消失时也必须清空记账，否则下次联合矩形算错）。教训：双页架构下，可见页质量 = 每一笔穿透写的时序总和；擦除尽量合并、且只落在无新内容的区域。
- **关键教训：SDRAM 刷新率不足导致 BTE 重载时屏幕花点**。所有数据路径验证干净（CRC+回读抽样+band scan）但 BTE blit 时读数周围出现固定红绿蓝单点，提高刷新率呈单调剂量响应：V16 值 `REG[E2/E3]=0x01E6` → 2x 减少 → 4x 更少 → **8x（`0x003C`）仅剩少数** → 16x 无进一步收益。最终固定 8x，写入 `configure_sdram()` 并注释依据。残余少量花点属 BTE 与显示扫描瞬时带宽竞争，如需彻底消除可用 BTE 分帧限流。
- **花点根因定案（2026-08-21 深度排查）**：①**V16 固件的 PLL 值超规格**——`MCLK=CCLK=200MHz` 超出 DS V3.0 绝对上限 `CLKMPLL=133MHz`（表 5-2），SDRAM 采样边缘不稳定；已降为 **100MHz**（`REG[07..0A]` C2=`0x32`），约束 `CCLK×2≥MCLK≥CCLK≥PCLK×1.5` 全部满足；50MHz 实测低于 SDRAM 最低工作频率（GE 清屏超时），可用窗口 100~133MHz。②PCLK 从 50MHz 降至 **25MHz**（`C2=0x0C`）加宽扫描截止期。③刷新率按 MCLK 减半补偿后取 `0x0F`。④**隐藏页渲染**：运行帧合成在 `visible^1` 页、提交时原子翻 MISA；`ui_fill_rect/ui_draw_line/blit` 全部双页写保持两页一致；逐页快照（trend/text generation）发布到两页防止翻页诱发全量重绘。⑤**差分渲染**：tile 自带不透明黑底可自擦除，item0 规划本帧 cell 表（按位置判占用来决定是否预擦除——按字形身份预擦会造成"等 VSYNC 期间空白"），未变化 cell 跳过 blit。⑥**残余花点为硅级固有缺陷**：BTE→SDRAM 写路径在特定数据/几何组合下确定性产出错误像素——证据：资源 PC 解码零杂散、缓存全量审计零失配、隐藏页仍花点、缓存基址平移 1KB 只改数量不改位置。软件已无更多抓手，如需彻底消除只能绕开 BTE（MRWDP 直写，速度不可用）。
- LT7680A-R + ST7701S 的初始化细节（SPI 协议、PLL/SDRAM/时序寄存器、已验证的 V16 寄存器值、彩条验收、黑屏排查顺序）见 skill `.agents/skills/lt7680-st7701/SKILL.md`；烧录用 `.agents/skills/openocd-stm32-flash/SKILL.md`。
- 可用 `arm-none-eabi-objcopy -I ihex -O binary <firmware.hex> <firmware.bin>` 将 HEX 转为二进制供静态分析；转换不会恢复源代码或协议语义。

## 自研部分的开发流程（仿真器 ↔ 固件骨架 ↔ CubeMX 三处同步）

三层代码共享同一套 UI/协议逻辑，改任何一层都必须同步另两层：

- **镜像源**：`firmware/src/` 里的纯逻辑模块（`ui_model`、`main_display`、`status_bar`、`k2000_proto`、`reading_split`、`panel_transform`、`scene`、`keypad`、`font_digits`、`font_half`、`font_text`）与 CubeMX 树 `KEITHLEY_2000_LCD/Core/{Src,Inc}/` 的对应文件**逐字节保持一致**（仓库约定手工 `cp` 同步，无脚本）。改一处即 `cp` 同步并 `diff` 确认。硬件驱动（`lt7680_bus`、`lt7680_gfx`、`hal_board`、`main.c`）两树允许有差异：`firmware/src` 是离线骨架（含 `hal_stub.c`），CubeMX 树接真实 HAL。
- **固件逻辑测试**：`firmware/` 下 `./tests/run_tests.sh`（宿主 gcc 编译 15 个 `tests/test_*.c`，`SRCS` 列表要随 src/ 新增文件手工维护）。跑测试**不需要** arm-none-eabi-gcc。
- **离线裸机编译**：`firmware/` 下 `make`（arm-none-eabi-gcc `-mcpu=cortex-m3 -mthumb`，`-Werror`），产物 `firmware/build/firmware.elf`。与 CubeMX 工程**无关**，只验证骨架能过编译。
- **布局仿真器**：`sim/index.html`（单文件，逐像素预览 `main_display_format` 输出，无需烧录）。数据与逻辑全部来自 `firmware/src`，加载 `sim/font_data.js`。用法见 `sim/README.md`。
- **无头一致性验证**：`node sim/verify.js`（验证布局常量、右对齐、特殊色、字形墨点数、render() 冒烟）。改字体或布局后必须跑。
- **字体再生成**：MCU 不再内置 64x128 大数字位图；其归档生成源在 `tools/font_source/font_digits.[ch]`，用于 RIF 打包与仿真器。运行 `python3 tools/make_sim_font.py firmware/src sim/font_data.js` 时，大数字取自该目录，半高/文本仍取自 `firmware/src`（勿手改 `font_data.js`）。`font_half`（半高 DC/AC，32x64）用 `--width 32 --height 64 --charset "DCA" --symbols ""` 重新生成后仍须 `cp` 到 CubeMX 树。
- **⚠️ 大数字库基线约束**：RIF 大字仍使用 64x128 单元与 `FONT_DIGIT_BASELINE=103`，字符集含 `H z s` 与独立符号表 `µ ° Ω`；半高 `D C A` 在 `font_half.c`。修改 `tools/font_source` 字形后，重新打包 RIF 并重跑 `verify.js`。
- **布局工作台**：仿真器内置「布局参数」面板可拖/调布局并导出 `main_display.h` 覆盖宏；「重置布局」恢复与 `main_display.h` 一致的默认值。设计目标 1920×1080 视口。
- 场景/状态相关决策记录在 `docs/adr/`（如 0001 视图空间 vs 帧缓冲变换）；范围与验收清单在 `docs/superpowers/plans|specs/`；术语表在 `CONTEXT.md`。

## 版本与许可

- `Change Log V16.txt` 只记录一项 V15 变更：图形加速控制板 Reset 线改为开漏；不要据此假定完整版本差异。ODS 的 `TX` 表还标记部分按键为 `Confirmed`、`Char unknown` 或 `Not wired`，这些状态必须保留在协议/键盘分析记录中。
- 上游 `LICENSE.md` 是 CC BY-NC-ND 4.0。共享上游材料需保留署名和许可信息；不要发布基于上游材料的改编版本或闭源固件的反编译代码，许可边界有疑问时先停下核实。

## 目录

- `KEITHLEY_2000_LCD/`：自研 STM32CubeMX 工程（真机固件，唯一烧录来源）。
- `firmware/`：离线骨架 + 宿主测试（`firmware/src/` 是镜像源，`tests/` 单测）。
- `sim/`：布局仿真器（`index.html` + 生成的 `font_data.js` + `verify.js`）。
- `tools/`：字体生成器（`make_digit_font.py`、`make_text_font.py`、`make_sim_font.py`）与资源镜像打包/校验（`pack_resource_flash.py`、`verify_resource_flash.py`、`rif_common.py`，见 `tools/README.md`）。
- `docs/`：确定性网表 `KEITHLEY2000_2026-08-08.tel`（权威）、ODS 协议表、`adr/` 决策记录、`superpowers/plans|specs/` 范围与验收、2026-08-14 仿真器 vs ODS 核对笔记。
- **RIF 资源镜像约定**：U5 挂在 LT7680 SPI 上、不经 STM32；实物以 flashrom 的 `EF4017` 识别为 W25Q64JV（8MiB），尽管网表记录 W25Q128JV。`tools/pack_resource_flash.py` 从 `tools/font_source` 的归档大数字位图与 `firmware/src` 的半高字库产出 RGB565 瓦片镜像（4 KiB 对齐，自描述头 + 目录 + CRC32）。运行时由 LT7680 SPI-master FIFO 只读目录和瓦片；写入前必须先备份原片并确认 `--base-offset`，验收见 `tools/README.md`。
- `Firmware STM32_K2000 DisplayBoard TFT_V16/`：上游 V16 HEX 与变更日志；根目录另有 V15 Flash 数据与 ODS。
- 自研技能在 `.agents/skills/`（lt7680-st7701、openocd-stm32-flash、lt7680-render-coherence 等）。渲染一致性经验（换挡原子行、失败的镜像实验、阶段计时诊断）沉淀在 `lt7680-render-coherence/SKILL.md`，动渲染前先读。
- PERF 常驻换挡诊断字段：`dbg_func_change/dbg_info_repaint/dbg_stale_kill/dbg_present_hold` ＋九阶段累计 `stg=`（IDLE/STATUS/INFO/CLEAR/VALUE/UNIT/SUFFIX/TREND/PRESENT）。健康换挡：`func_change≤2/info_repaint≤2/stale_kill=0`、`missed=0`；`max-ms` 为启动累计最大值，窗口分析看 `frame-ms`＋`stg`。
