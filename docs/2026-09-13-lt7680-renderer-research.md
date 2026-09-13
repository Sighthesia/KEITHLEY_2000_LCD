# LT7680 渲染器替代方案检索记录

日期：2026-09-13

## 型号确认

未找到与 TFT 图形控制器对应的 `LT7860` 公开资料。当前项目应以实际芯片丝印和资料为准，现有工程目标是 `LT7680A-R`。`RA8876` 和 LT768x 的寄存器结构、BTE、外部 SDRAM 和 Serial Flash DMA 功能高度兼容，但不能在未核对芯片版本前假定全部时序和容量相同。

## 官方资料

- [Levetop LT768x Complete Datasheet V4.2](https://www.levetop.cn/uploadfiles/2023/05/LT768x_DS_V42_ENG.pdf)：BTE、显示窗口、SDRAM、Serial Flash DMA、文本引擎和寄存器定义。
- [Levetop LT768x Application Note V1.2](https://www.levetop.cn/uploadfiles/2023/05/LT768x_AP-Note_V12_ENG.pdf)：STM32F103/LT768x 示例、BTE、DMA、外部 Flash、显示初始化和示例工程章节。
- [Levetop LT768x TFT Panel Display-On Process](https://www.levetop.cn/uploadfiles/2023/05/LT768x_DisplayOnTFT_V10.pdf)：复位、状态检查、SDRAM 和 RGB/PCLK/HSYNC/VSYNC 排查。
- [Levetop LT7680 下载页](https://www.levetop.cn/en/Download/list.aspx?lcid=243&pid=243)：列出 `LT7680/1/3/6 STM32 Demo-kit Program`、`STM32F103 + LT7680 AP Circuit` 和 LT7680 相关函数库/例程。
- [LT7680 STM32F103 Demo 电路资料](https://www.levetop.cn/uploadfiles/2023/07/LT7680_STM32F103_Demo_V1.0.pdf)：STM32F103 与 LT7680 参考连接资料。

官方资料支持的基本架构是：LT7680 内部完成图形操作，外部 Serial Flash 作为图像/字形源，多个 SDRAM buffer 用于后台绘制、BTE copy 或显示地址切换。较大的更新不应由 MCU 逐像素写入。

## 可用例程和兼容实现

- [RA8876 Lite User Guide](https://www.raio.com.tw/data_raio/RA887677/Arduino/RA8876_Lite_UserGuide_v1.0_Eng.pdf)：BTE memory copy、ROP、Chroma Key、Color Expansion、Serial Flash DMA、ping-pong buffer 和 VSYNC 示例。
- [TechToys HDMI-Shield/Ra8876_Lite](https://github.com/techtoys/HDMI-Shield/tree/master/Ra8876_Lite)：RA8876 C++ 驱动和可运行例程，适合参考多 buffer、BTE 和 DMA 调用顺序。
- [xlatb/ra8876](https://github.com/xlatb/ra8876)：SPI、状态读取、SDRAM、窗口和基础 BTE 实现参考。
- [danmeuk/esp_lcd_ra8876](https://github.com/danmeuk/esp_lcd_ra8876)：RA8876/LT768x 的 ESP-IDF panel 封装，适合参考 panel/flush 边界，不适合直接移植到 STM32F103 SPI。
- [wwatson4506/TeensyRA8876-GFX-Common](https://github.com/wwatson4506/TeensyRA8876-GFX-Common)：通用 RA8876 图形层，包含 BTE 和 DMA 相关示例，但目标平台是 Teensy。
- [ToSStudio/LT7683](https://github.com/ToSStudio/LT7683)：轻量 LT7683/RA8876 Arduino 库，强调硬件 busy 轮询；可作寄存器访问参考，不能替代 LT7680 官方验收。
- [LVGL RA8876 SDRAM discussion](https://github.com/littlevgl/lvgl/issues/512)：说明 LVGL 外部 SDRAM 不能直接当作 MCU 可寻址 framebuffer，必须通过块 flush、DMA 或 BTE 间接更新。

## 与当前问题直接相关的资料结论

### BTE/DMA 完成边界

公开例程将普通绘图、2D/BTE、Serial Flash DMA 和 SDRAM ready 分开等待。下一次操作前必须确认上一条内部操作完成；MCU 完成 SPI 命令发送不等于 LT7680 已完成 SDRAM 搬运。

### Canvas 和 stride

Main Window、Canvas Window、Active Window 的地址、宽度和范围必须成组配置。BTE source width、destination width 和 DMA source width 必须对应实际每行 stride，不能只使用本次矩形的可见宽度。stride 错误可导致黑条、逐行错位、局部花屏和移动黑块。

### 多缓冲

RA8876/LT768x 例程普遍采用：当前页保持显示，后台页完成 DMA/BTE/绘制，完成后切换显示起始地址，或使用 BTE 将完整更新区域复制到显示页。运行期间持续写扫描中的页面会增加花点和撕裂风险。

### VSYNC

VSYNC 可用于选择安全的页面切换时机，但不能替代 DMA/BTE busy 等待。先等待内部操作结束，再在合适的 VSYNC 边界切换显示页，是公开例程中的推荐方向；具体锁存行为仍需在本项目 LT7680 版本上实测。

### 字形和性能

当前项目历史记录与公开资料一致：逐像素或逐字 Flash DMA 会显著降低刷新率；预加载到 LT7680 SDRAM 后用 BTE 搬运字形是合理的高性能路径。BTE 连续操作仍可能与显示扫描产生 SDRAM 竞争，因此需要正确的 busy 顺序、页面隔离和必要的节流。

## 对本项目的建议

不建议直接引入完整 LVGL。STM32F103C8T6 的 RAM/Flash 资源有限，LVGL 仍需要可靠的 LT7680 flush、BTE、DMA 和页面提交底层；它不能自动修复当前的地址或同步问题。

建议保留现有协议解析、主机快照、UI 模型和逻辑坐标布局，只替换渲染底层为以下分层：

```text
k2000 model
    -> view layout
    -> framebuffer/page manager
    -> LT7680 BTE layer
    -> LT7680 Serial Flash DMA layer
    -> LT7680A-R
```

迁移/验证顺序：

1. 用官方 STM32F103/LT7680 初始化顺序确认 SDRAM 和 panel timing。
2. 固定红/绿/蓝页面验证 Main Window 和 Canvas stride。
3. 用两个固定页面验证后台绘制和页面切换。
4. 用 32x32 RGB565 图案验证 BTE source/destination geometry。
5. 用标准连续 RGB565 图像验证 Serial Flash DMA 的 source width 和 block geometry。
6. 只接入一个数字字形，再接入动态读数。
7. 最后恢复趋势图和完整页面更新。

## 可信度边界

- Levetop/RAiO PDF 和 Levetop 下载页是型号、寄存器和官方例程的主要依据。
- GitHub 驱动和 Arduino/ESP-IDF 实现是调用顺序及工程实践参考，不是本项目芯片版本的最终规格依据。
- VSYNC 页面锁存时机、特定 SDRAM 容量设置和本项目 RIF tile 的可用 stride 必须通过当前硬件实测，不能仅由兼容芯片代码推断。
