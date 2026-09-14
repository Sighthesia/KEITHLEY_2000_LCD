# LT7680/RA8876 未决问题研究

日期：2026-09-13

范围：只读调查当前工程涉及的 Serial Flash DMA、BTE、GE、Main/Canvas/Active Window、双缓冲、外部字库和 SDRAM。结论优先采用 Levetop/RAiO 官方资料；开源驱动只作为实现交叉检查，不作为 LT7680A-R 的最终规格依据。

## 结论摘要

1. **Canvas/Main Window 的宽度寄存器以像素为单位，而不是字节。** `MIW`、`CVS_IMWTH`、BTE 的 source/destination image width 和 DMA 的 source picture width 都描述图像行宽；16bpp 的字节跨度由像素宽度和色深共同决定。当前项目的 `canvas_stride=320` 是合理的 Canvas 像素 stride；RIF 的 `stride=0x0100` 若表示 256 字节/行，则对应 128 个 RGB565 像素，不能直接把 256 写入 16bpp 的像素宽度寄存器。**但 DMA 的 `DMAW_WTH` 对“块宽”和 `DMA_SWTH` 对“源图宽”在 LT7680A-R 这一具体版本上的 16bpp 解释，仍应以固定图案实测确认。**
2. **命令写完不等于硬件搬运完成。** GE/BTE/DMA 都必须等待各自完成条件；公开 RA8876 驱动通常轮询 Status 的 2D/CORE busy 位或使用 WAIT 信号。当前工程的 `lt7680_gfx.c` 使用 `STATUS bit3` 等待 GE、`DMA_CTRL` 空闲等待 Flash DMA；BTE 需确认它是否也被同一个 CORE busy 覆盖，不能只依赖 SPI 发送完成。
3. **双缓冲的核心关系是 MISA 指向正在扫描的显示页，CVSSA 指向当前绘图 Canvas；两者可以相同，也可以在后台页绘制时分离。** Canvas width 必须是整页的物理行宽，不是当前矩形宽度。切换 MISA 前应完成后台 DMA/BTE/GE，并尽量在 VSYNC 安全点切换；VSYNC 不能替代 busy 等待。
4. **`MemoryAddr` 是 LT7680 内部 SDRAM 工作区地址，不是 STM32 地址。** 标准外部字库由 LT7680 文本引擎按照字库格式解析，不能把任意 RIF/RGB565 payload 当作标准外部字库。字库工作区必须避开 Canvas、显示页、趋势图和 DMA staging 区。
5. **SDRAM 参数具有芯片/颗粒边界。** `SDRAR/SDRMD/refresh interval/SDRCR` 必须匹配实际 SDRAM 的 bank、row、column、CAS、时钟和刷新要求。公开 RA8876 驱动按 `refresh_ms * memory_clock / 2^row_bits` 计算刷新间隔并等待 SDRAM ready；本项目目前的刷新加速是硬件经验值，不能由 RA8876 示例直接证明安全。

## 1. BTE、Flash DMA：stride 和宽度单位

### 官方证据

| 结论 | 来源与定位 | 适用边界 | 可信度 |
|---|---|---|---|
| Main Image、Canvas、Active Window 是独立但必须配套的几何；Main/Canvas image width 作为每行图像宽度配置。 | Levetop [LT768x DS V4.2](https://www.levetop.cn/uploadfiles/2023/05/LT768x_DS_V42_ENG.pdf)，§10.2.1--10.2.3，PDF pp.50--52；寄存器 `20h--29h`、`50h--5Eh` | LT768x 家族；需确认 LT7680A-R 是否为同一 revision | 高 |
| BTE 有 source 0/1、destination start address、各自 image width、坐标和 BTE width/height；image width 是源/目的图像的行宽，不是本次可见矩形宽度。 | 同上，§12.1--12.3，PDF pp.64--72；`93h--B4h` | LT768x；RA8876 同类寄存器映射可交叉验证 | 高 |
| Serial Flash DMA 明确区分 source starting address、destination x/y、block width/height 和 source picture width。 | Levetop [LT768x AP Note V1.2](https://www.levetop.cn/uploadfiles/2023/05/LT768x_AP-Note_V12_ENG.pdf)，§11 DMA；本地中文手册 pp.34--35 `LT768_DMA_24bit_Block(...)` | 官方示例主要是整图/packed RGB，不能直接证明项目自定义 RGB565 tile 的单位 | 高，但对本项目 tile 为间接证据 |
| RAiO DMA 示例以“原始图片宽度”和目标块宽高分别配置；Serial Flash 图像由 Image Tool 生成并携带宽度、格式、大小和起始地址信息。 | RAiO [RA8876_77 AP User Guide](https://www.raio.com.tw/data_raio/RA887677/AP/RA8876_77_AP_User_Guide_v0.2_EN.pdf)，§6 DMA，PDF pp.25--29；公开摘要明确列出 source start、original image width、destination x/y、block width/height | RA8876/RA8877 AP/例程；不等于 LT7680A-R 的完整兼容声明 | 高 |
| 维护良好的 RA8876 驱动把 `MIW`、`CVS_IMWTH`、`AW_WTH` 写成显示像素宽度；`setCanvasRegion`/`setDisplayRegion` 要求宽度按硬件限制对齐。 | [xlatb/ra8876 `src/RA8876.cpp`](https://github.com/xlatb/ra8876/blob/master/src/RA8876.cpp)，`initDisplay()`, `setCanvasRegion()`, `setDisplayRegion()`；[register map](https://github.com/danmeuk/esp_lcd_ra8876/blob/main/ra8876_registers.h)，`MISA`, `MIW`, `CVSSA`, `CVS_IMWTH`, `DMAW_*`, `DMA_SWTH` | RA8876 Arduino；作者注释称寄存器也适用于 LT768x，但没有 LT7680A-R 实测证明 | 中高 |

### 对当前工程的判断

- `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c:511--523` 将 `block_width_pixels` 写入 `DMA_WTH` 和 `DMA_SWTH`，并将 `destination_stride_pixels` 写入 Canvas stride。这符合“寄存器按像素宽度、Canvas stride 为整行像素数”的主线，但**尚未证明 RIF 的 `width_bytes` 到 `block_width_pixels` 的换算在所有 DMA 模式都正确**。
- `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c:956` 附近的窗口初始化把 `MISA/CVSSA` 与 Canvas/Main width 分开配置，这与官方窗口模型一致。
- 当前 RIF 记录 `80x44, stride=0x0100` 的关键分歧是：如果 stride 是 256 bytes/row，RGB565 行宽是 128 pixels；如果资源已经预转置，DMA 的“source picture width”还必须描述 Flash 中的实际连续行，而不是逻辑屏幕宽度。
- **只能本板实测的问题**：对 LT7680A-R 实际 die，16bpp Serial Flash DMA 是否把 `DMAW_WTH/DMA_SWTH` 解释为 pixels、是否要求 16-bit word count、是否对 source width 施加 4/8 像素对齐，以及目的 Canvas stride 在非线性 Block mode 下的确切寻址方式。应使用纯色、棋盘格、每行不同颜色的固定图案，读取 Canvas 样本并观察屏幕，分别测试 1、2、4、8、16、32 像素宽度。

## 2. DMA/BTE/GE busy 完成条件

### 已确认的模式

- 官方 RA8876_Lite 文档把 BTE 和 DMA 作为独立功能章节：BTE §7（PDF pp.58--80），DMA §8（PDF pp.81--86）。这意味着“启动命令已经通过 SPI”不能作为内部操作完成的条件。
- [xlatb/ra8876 `RA8876.cpp`](https://github.com/xlatb/ra8876/blob/master/src/RA8876.cpp) 的 `drawTwoPointShape()`、`drawThreePointShape()`、`drawEllipseShape()` 在启动 GE 后循环读取 `readStatus()`，直到 `status & 0x08` 清零；`softReset()` 等待 `status & 0x02` 清零；`initMemory()` 等待 `status & 0x40` 置位。
- [danmeuk/esp_lcd_ra8876 `esp_lcd_ra8876.c`](https://github.com/danmeuk/esp_lcd_ra8876/blob/main/esp_lcd_ra8876.c) 的 `panel_ra8876_wait()` 使用 RA8876 WAIT GPIO；初始化后在 SDRAM ready、显示操作边界等待该信号。该实现并未证明每一个 LT7680A-R DMA/BTE busy 位的具体映射。
- RAiO [RA8876 Lite User Guide](https://www.raio.com.tw/data_raio/RA887677/Arduino/RA8876_Lite_UserGuide_v1.0_Eng.pdf) 的 BTE/DMA 示例和官方 API 流程适合确认操作顺序；但公开文本不能替代目标芯片 Status bit 定义和 WAIT 极性核对。

### 当前工程的未决点

- `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c:1605` 的 `wait_dma_idle()` 轮询 `DMA_CTRL`；这是 DMA 专用完成条件。
- GE 填充路径在当前工程中按 `STATUS bit3 (0x08)` 等待，和 RA8876 开源驱动的 GE 轮询一致。
- BTE 的等待必须明确回答：BTE 启动后是等待 `STATUS bit3` 清零，还是还需等待 `DMA_CTRL`、`SPIMSR`、WAIT/INT 或其他状态。官方资料把 BTE、GE、DMA 分开描述，但当前公开材料没有给出足够清晰的“跨引擎统一 busy”保证。
- **建议的实测判据**：每次只启动一个引擎；在启动后立即连续采样 `STATUS`、`DMA_CTRL`、`SPIMSR` 和相关寄存器；确认目标 RAM 的哨兵值在 busy 清零后才稳定；再故意在 busy 期间发起下一条 GE/BTE，观察是否丢命令或污染目标。
- **只能本板实测**：Status bit 的清零是否早于最后一个 SDRAM 写入、WAIT 是否可靠、BTE 与 GE 是否共享 busy、DMA 完成后是否还需额外 SPI FIFO 空闲时间，以及 VSYNC/INT 的锁存时机。

## 3. Canvas、MISA、CVSSA 与双缓冲

### 资料结论

- 官方 RAiO 指南的 Memory Configuration & Window 章节为 RA8876 配置多个 SDRAM page；BTE 章节使用后台 image buffer，再把后台页复制到显示页以减少 flicker/overlap。[RA8876 Lite User Guide](https://www.raio.com.tw/data_raio/RA887677/Arduino/RA8876_Lite_UserGuide_v1.0_Eng.pdf)，§3 PDF pp.17--22、§7 PDF pp.58--80。
- RAiO [RA8876_77 AP User Guide](https://www.raio.com.tw/data_raio/RA887677/AP/RA8876_77_AP_User_Guide_v0.2_EN.pdf)，§8.1 Ping-pong Buffer，PDF pp.34--36，明确把显示 buffer 与后台 buffer 分离，再切换/复制。
- 开源 [xlatb/ra8876 `setCanvasRegion()` / `setDisplayRegion()`](https://github.com/xlatb/ra8876/blob/master/src/RA8876.cpp) 将 `CVSSA` 与 `CVS_IMWTH` 作为 Canvas 配置，将 `MISA` 与 `MIW` 作为显示区域配置；`AW_COLOR` 的 bit 2 控制 linear/block addressing，16bpp 选择 `0x01`。

### 对当前工程的适用解释

- `MISA` 是扫描输出的 Main Image 起始地址；`CVSSA` 是图形操作/Canvas 的起始地址。后台绘制时可以保持 `MISA=visible_page`，把 `CVSSA=render_page`，完成后再切换 `MISA`，并把 Canvas 重新指向后续绘图页。
- Canvas width 应保持整页 stride，例如当前 320 像素，而不是某个 `80x44` glyph 的 80 像素。BTE source/destination width 同样应使用源/目标图像的实际行宽；矩形可见宽度只写入 BTE transfer width。
- 当前工程 `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c` 的 `lt7680_gfx_select_canvas_page()`、`lt7680_gfx_set_canvas_base()`、`lt7680_gfx_set_canvas_width()` 和 `lt7680_gfx_blit()` 是这一模型的主要实现位置；`main.c` 的 `lt7680_gfx_present_page()` 是提交边界。
- 1 MiB page stride 是当前工程的布局约定，不是芯片固定规则。它必须小于实际 SDRAM 容量，并覆盖 `width * height * bytes_per_pixel` 加上硬件寻址限制；地址 4-byte 对齐是公开 RA8876 驱动的最低要求，LT7680A-R 是否有更严格限制需核对其 revision。
- **只能本板实测**：MISA 多字节写入时的中间地址是否会被扫描器短暂采样、写入 MISA 的实际锁存时刻、是否必须 VSYNC 边界、以及 LT7680A-R 的 block/linear mode 对 Canvas stride 的真实影响。

## 4. 外部字库格式与 MemoryAddr

### 官方资料

- 本地 Levetop 中文手册 [乐升LT系列芯片_使用手册_V1.0.pdf](./乐升LT系列芯片_使用手册_V1.0.pdf)，PDF pp.36--37：`LT768_Select_Outside_Font_Init(...)` 与 `LT768_Print_Outside_Font_*` 示例；示例含 `MEMORY_16`、`MEMORY_32`、Flash 字库地址和 `MemoryAddr`。文字警告 `MemoryAddr` 不能与其他功能使用的 SDRAM 地址重叠。
- Levetop [LT768x DS V4.2](https://www.levetop.cn/uploadfiles/2023/05/LT768x_DS_V42_ENG.pdf)，§16 Text Function / 外部 Font ROM 相关寄存器，及寄存器 `CCRAM/CGRAM`、`GTFNT_SEL/GTFNT_CR`；适用于 LT768x 文本引擎，不证明任意用户资源格式兼容。
- RAiO [RA8876 Lite User Guide](https://www.raio.com.tw/data_raio/RA887677/Arduino/RA8876_Lite_UserGuide_v1.0_Eng.pdf)，§5 Text and Value，PDF pp.32--46：外部 Genitop Font ROM、ASCII/BIG5/GB2312 文本路径；外部字库不是简单 RGB565 frame tile。

### 适用于当前项目的结论

- `MemoryAddr` 应理解为 LT7680 外部 SDRAM 中由文本引擎使用的工作/缓存地址；不能填 STM32 Flash/RAM 指针，也不能与 page 0/page 1、temporary/staging、glyph cache 重叠。
- 当前项目 `rif_tile_cache.c` 的“读取自定义 RIF payload -> 写入 SDRAM -> BTE”路径，不等同于标准外部字库文本引擎。`cache=OK` 或单像素 probe 不能证明 RIF payload 符合 Levetop/RAiO 字库格式。
- 当前已记录的区域：page 0 `0x000000`、page 1 `0x100000`、temporary `0x200000`、glyph cache `0x300000`。仍需按完整 tile 高度、stride、DMA 临时区和实际 SDRAM 容量做范围验证。
- **只能本板/资源实测**：RIF Flash 是否是标准外部字库、字模编码/索引格式、`MemoryAddr` 所需工作区大小、外部 Font ROM/Serial Flash 片选与协议配置，以及 LT7680A-R 是否支持该字库类型。最小实验应先用官方格式的一枚静态字形，再与自定义 RGB565 tile 路径分开验证。

## 5. SDRAM 地址、刷新和时钟注意事项

### 官方与开源证据

| 结论 | 来源与定位 | 适用边界 | 可信度 |
|---|---|---|---|
| SDRAM 配置由 attribute、mode、refresh interval 和 control/start 组成；初始化后必须等待 ready。 | Levetop [LT768x DS V4.2](https://www.levetop.cn/uploadfiles/2023/05/LT768x_DS_V42_ENG.pdf)，§8 Display Memory、§19 register descriptions；本地手册初始化与 pp.34--37 示例 | LT768x；颗粒参数仍需本板核对 | 高 |
| RA8876 开源驱动按 bank、row bits、column bits、CAS 生成 `SDRAR/SDRMD`，按 `refresh_ms * speed * 1000 / 2^row_bits` 计算 refresh interval，并轮询 Status bit `0x40` 等待 SDRAM ready。 | [xlatb/ra8876 `initMemory()`](https://github.com/xlatb/ra8876/blob/master/src/RA8876.cpp)，`defaultSdramInfo` 与 `initMemory()` | RA8876 16-bit SDRAM 配置；不能直接证明 LT7680A-R 的 status bit 完全相同 | 中高 |
| RA8876/RA8877 支持多 buffer、BTE、DMA 和外部 SDRAM；实际容量/接口由具体型号和板级颗粒决定。 | RAiO [RA8876/77 product page](https://www.raio.com.tw/en/RA887677.html) | RA8876/RA8877，不自动覆盖 LT7680A-R | 高 |
| 当前项目曾将刷新间隔从 V16 值加速到 `0x003C` 以减少实测 sparkles。 | 当前工程 `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c:863--894` | 仅本项目当前板、当前时钟/温度/SDRAM 组合的经验 | 中，需复测 |

### 风险判断

- 刷新值不是越小越安全：过快会增加控制器/SDRAM 带宽占用，过慢会出现随机单像素错误、字形边缘污染或长期数据保持失败。应由实际 SDRAM 的 `tREFI`、MCLK、row 数和温度范围推导，再用长时间 BTE/GE/DMA 压力测试验证。
- SDRAM 地址是控制器内部地址空间，不是 STM32 可直接解引用的外部内存。Canvas、MISA、BTE source/destination、DMA destination 和 `MemoryAddr` 必须统一使用 LT7680 SDRAM 地址，并检查对齐、容量和最后一行边界。
- PLL/MCLK/CCLK/PCLK 约束必须与芯片版本相符。当前工程注释称旧 V16 参数超过 DS V3.0 MCLK/CCLK 限制；这个判断应以 LT7680A-R 对应 revision 的最大频率表为最终依据。
- **只能本板实测**：外部 SDRAM 实际型号/容量/位宽、刷新值在当前 10 MHz 晶振和 PLL 下的裕量、温升后的保持能力、BTE/显示扫描竞争，以及高频 DMA 后是否出现随机 bit error。建议做整夜压力测试：固定页 CRC、GE 填充、BTE copy、Flash DMA、MISA 切换交替运行，并记录错误地址和温度。

## 6. 当前未决问题清单

| 优先级 | 未决问题 | 现有证据 | 只能实测？ |
|---|---|---|---|
| P0 | RIF `stride=256` 是字节 stride 还是像素 stride？DMA `DMAW_WTH/DMA_SWTH` 在当前 LT7680A-R 16bpp 路径的单位是什么？ | 官方 DMA 只给整图示例；当前代码将 width 转为像素，但资源格式未由官方资料定义 | 是，需固定图案 + Canvas 读回 + 屏幕观察 |
| P0 | BTE 完成是否完全由 `STATUS bit3` 表示？DMA 完成后是否还要等待 SPI master/FIFO/WAIT？ | 开源 GE 轮询 bit3；官方章节分离 BTE/DMA；没有足够的 LT7680A-R 跨引擎保证 | 是 |
| P1 | MISA 多字节更新何时锁存？是否需要 VSYNC？ | RAiO ping-pong 资料支持后台页/页面切换；具体锁存时序未明确 | 是 |
| P1 | Canvas width 在 block mode 下是否必须整行像素宽、4/8 像素对齐？ | 开源 RA8876 驱动这样约束；LT7680A-R revision 边界未完成核对 | 部分是 |
| P1 | RIF 是否为标准外部字库，或只是项目自定义 RGB565 tile？ | 官方字库接口与项目 RIF 路径是两套模型 | 是，必须解析资源并做最小显示实验 |
| P1 | `MemoryAddr` 需要多少 SDRAM 工作区，是否与现有四段区域冲突？ | 官方只给“不重叠”原则和示例地址 | 是 |
| P2 | `0x003C` refresh interval 是否在所有温度/压力下可靠？ | 当前板历史经验与开源公式不完全相同 | 是，需长时间压力与温度覆盖 |

## 7. 参考资料总表

- Levetop, [LT768x High Performance TFT-LCD Graphics Controller Data Sheet V4.2](https://www.levetop.cn/uploadfiles/2023/05/LT768x_DS_V42_ENG.pdf)，§8、§10、§11、§12、§16、寄存器章节。
- Levetop, [LT768x Application Notes V1.2](https://www.levetop.cn/uploadfiles/2023/05/LT768x_AP-Note_V12_ENG.pdf)，§8、§10、§11；STM32/LT768x 示例。
- Levetop, [LT7680 download/resources page](https://www.levetop.cn/en/Download/list.aspx?lcid=243&pid=243)，LT7680 STM32 demo-kit、STM32F103 reference material 和函数库入口。
- RAiO, [RA8876 Lite User Guide V1.0](https://www.raio.com.tw/data_raio/RA887677/Arduino/RA8876_Lite_UserGuide_v1.0_Eng.pdf)，§3、§5、§7、§8。
- RAiO, [RA8876/77 AP User Guide V0.2](https://www.raio.com.tw/data_raio/RA887677/AP/RA8876_77_AP_User_Guide_v0.2_EN.pdf)，§6 DMA、§8 ping-pong、§9 BTE。
- xlatb, [ra8876](https://github.com/xlatb/ra8876)，`src/RA8876.cpp` 与 `src/RA8876.h`，Arduino RA8876 实现；参考代码，非 LT7680A-R 官方实现。
- danmeuk, [esp_lcd_ra8876](https://github.com/danmeuk/esp_lcd_ra8876)，`esp_lcd_ra8876.c` 与 `ra8876_registers.h`，ESP-IDF RA8876 panel 封装；参考寄存器名、WAIT 和窗口初始化。

## 可信度说明

- **高**：Levetop/RAiO 官方数据手册、官方用户指南、官方应用笔记直接描述的寄存器和 API。
- **中高**：维护良好的开源驱动中重复出现的寄存器地址、像素宽度配置、SDRAM 计算和 busy 轮询；它们能说明常见 RA8876 实现，但不能替代 LT7680A-R revision 资料。
- **中/低**：当前工程历史实测、经验参数和单像素 probe。它们对本板有价值，但不能推广到其他 LT768x/RA8876 芯片、SDRAM 颗粒或时钟配置。

## Task 4 最终同步记录

2026-09-14 的验证确认：`rif_tile_cache.c` 在纯逻辑树和 CubeMX 树中逐字一致。两棵源码树的 `lt7680_gfx.c` 存在大规模差异；本次不声称已证明全部差异都是允许的硬件适配。仅确认新增 Task 2 相关范围校验在两树中的语义一致，完整共享逻辑同步仍需后续拆分和审计。

范围校验由固件测试和传输范围测试覆盖，包含目的地址非零 x 横向边界；RIF cache cleanup 的离线 mock 测试也已通过，恢复失败统一返回 `LT7680_ERR_BUS` 且 primary error 优先。这些测试不等于 LT7680 实机状态恢复已验证，Flash/BTE/GE 的完整状态恢复仍是未决。开源 RA8876 资料只能提供交叉检查，不能替代目标 LT7680A-R 和本板的固定图案、长时间压力实测。

本次状态仍不改变以下未决项：RIF 实际资源格式、LT7680A-R 16bpp DMA 单位语义、BTE 完成语义、MISA 锁存时机，以及长期 SDRAM refresh margin。
