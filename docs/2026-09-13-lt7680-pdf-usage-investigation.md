# LT7680 使用手册对照调查

日期：2026-09-13

## 资料

本记录依据仓库中的 [乐升LT系列芯片_使用手册_V1.0.pdf](./乐升LT系列芯片_使用手册_V1.0.pdf)，页码按 PDF 页面编号标注。当前 PDF 共有 38 页，文字层可正常读取；之前出现的“该模型不支持 PDF”是调用入口能力限制，不是文件损坏。

## 手册明确区分的三条路径

### 外部 Flash 图片 DMA

第 34--35 页给出 `LT768_DMA_24bit_Block(...)` 示例。示例特点：

- 数据源是 LT7680 的 CS1 外部 Flash。
- 示例图片为 `800*480`、`24bit`。
- 参数中的图像宽度、图像高度和源图起始地址共同描述一个连续的图像资源。
- 连续图片通过上一张图片的完整数据长度计算地址偏移；第 35 页特别提醒最后一个参数是图片起始地址。
- 第 34 页示例同时设置 Main Window、Canvas 和工作窗口，说明 DMA 目标布局依赖已经配置好的窗口/Canvas 几何。

这条路径适合整图或规则的 packed RGB 图像。它不能单独证明当前 `80x44` 字形 tile 的方向、字节序或 stride 参数。

### 外部字库/文本引擎

第 36--37 页使用 `LT768_Select_Outside_Font_Init(...)` 和 `LT768_Print_Outside_Font_*`。手册示例包括：

```c
LT768_Select_Outside_Font_Init(1, 0, 0x0, MEMORY_16,
                               0x0003FE40, 16, 1, 1, 0, 1);
LT768_Select_Outside_Font_Init(1, 0, 0x0003FE40, MEMORY_32,
                               0x000FF900, 32, 1, 1, 0, 1);
```

从文字说明和参数命名可确认：

- 字库资源位于外部 Flash，按字库格式和字库起始地址解释。
- `MemoryAddr` 是目标地址，也就是 LT7680 内部 SDRAM 中用于字库/绘制工作的地址，不是 MCU 地址。
- 第 37 页警告 `MemoryAddr` 不能与其他代码功能使用的 SDRAM 地址重叠。
- 外部字库路径由 LT7680 的文本引擎按字库格式解析，不等同于把 Flash 中的原始字节直接当作 RGB565 矩形搬运。

### Canvas/主窗口/工作窗口

第 34 页的图片示例同时出现以下配置语义：

```c
Main_Image_Start_Address(0);
Main_image_width(...);
Canvas_Image_Start_address(0);
Canvas_image_width(800);
```

第 36 页的外部字库示例也重复配置主窗口、Canvas 和工作窗口。这说明使用 DMA 或外部字库之前，必须先明确：

1. 显示主窗口从哪段 SDRAM 映射出来。
2. Canvas 起始地址是什么。
3. Canvas 每行宽度是多少。
4. 工作窗口坐标如何落到主窗口。
5. 资源目标地址是否避开其他 SDRAM 区域。

## 与当前工程的对照

当前工程中的 `lt7680_flash_dma_tile_to_canvas()` 做的是“外部 Flash -> 当前 Canvas 页”的自定义小块 DMA：

- 它把 `canvas_base` 和 `canvas_stride` 写入 `CVSSA/CVS_IMWTH`。
- 它把 `dx/dy` 写入 DMA 目标坐标。
- 它把 `width_px/height` 写入 DMA 块尺寸。
- 它等待 `DMA_CTRL` 空闲后恢复 Canvas base、Canvas stride、SPI DMA/FIFO 状态。

这与手册“外部 Flash 图片 DMA”的总体方向一致，但仍有四个未被手册示例证明的假设：

1. 当前 RIF tile 的 Flash 数据确实是 DMA 能直接消费的 packed RGB565，而不是字库位图、压缩数据或带行填充的数据。
2. 当前资源的 `stride=256` 是 DMA 所需的源图行宽，且单位应换算为 `128` 个 16-bit 像素，而不是直接写入 `256`。
3. 当前 tile 已经按面板 framebuffer 转置方向存储，因此 DMA 不需要旋转、翻转或交换 x/y。
4. DMA 完成后，仅恢复 `CVSSA/CVS_IMWTH/SPI` 就足以恢复后续 GE/BTE 的全部状态；手册示例没有覆盖本工程的双页 Canvas 和 BTE 组合。

当前工程中的 `rif_tile_cache.c` 另走一条自定义路径：逐行读取 Flash 数据，再用 `lt7680_gfx_write_pixels()` 写入 SDRAM cache，最后通过 `lt7680_gfx_blit()` 用 BTE 搬运。这不是手册第 36--37 页的外部字库文本引擎路径，而是“MCU 解析资源 + LT7680 SDRAM cache + BTE”的组合方案。

## 对当前黑块/花屏问题的直接结论

- `RIF sdram glyph cache=OK` 只能证明当前自定义 cache 构建过程返回成功，不能证明它符合 LT7680 外部字库格式。
- `RIF cache pixel probe=PASS` 只能证明有限像素读回成功，不能证明完整 tile 的行方向、stride、颜色顺序和目标页面正确。
- 手册中的图片 DMA 示例是 24 bpp 整图示例，不能用来证明当前 16 bpp、预转置、双页 `80x44` 字形的完整参数正确。
- 手册的外部字库示例提示了另一种正确用法：若资源确实是 LT7680 认可的外部字库格式，应使用外部字库初始化和文本输出 API，让 LT7680 解析字库，而不是把字库原始数据强行当作 RGB565 tile。
- 如果资源不是 LT7680 外部字库格式，而是项目自定义 RIF RGB565 tile，则必须继续使用当前自定义 cache/BTE 路径，但要用固定单字形实验逐项证明源地址、源 stride、tile 方向、像素格式、目标 Canvas 和页面切换。
- 第 37 页关于 `MemoryAddr` 不得与其他功能重叠的警告与当前问题高度相关。缓存区 `0x00300000`、临时区 `0x00200000`、Canvas 页 `0x00000000/0x00100000` 必须保持明确且不重叠；任何目标地址或 stride 错误都可能污染后续 GE/BTE 操作并表现为移动黑块。

## 推荐的正确使用顺序

### 若资源是标准外部字库

1. 按手册规定的字库格式准备 Flash 资源。
2. 按第 36--37 页调用 `LT768_Select_Outside_Font_Init(...)`，为每个字库指定正确的 Flash 起始地址、类型和 SDRAM `MemoryAddr`。
3. 为字库工作区预留不与 Canvas、双页 framebuffer、趋势图或临时 DMA 区重叠的 SDRAM 范围。
4. 使用 `LT768_Print_Outside_Font_*` 输出一个静态字形，先验证方向、颜色和位置。
5. 静态字形通过后再接入动态读数；不要同时启用自定义 RIF tile BTE 路径。

### 若资源是项目自定义 RGB565 RIF tile

1. 先读取一个 tile 的完整原始数据，确认每行实际字节数、像素端序、是否有 padding 和是否已经转置。
2. 用一个纯色/棋盘格 tile 写入 SDRAM staging 区，使用 `lt7680_gfx_blit()` 验证 BTE 源地址和源 stride。
3. 确认 BTE `source stride` 使用像素单位，目标 Canvas stride 使用完整 Canvas 行宽，而不是矩形可见宽度。
4. 单页完成后再验证 page 0/page 1；每次操作完成后分别等待 DMA、BTE 和 GE busy 清零。
5. 验证 DMA/BTE 后 `CVSSA`、Canvas width、当前显示页和目标页仍是预期值。
6. 最后才接入动态读数、擦除、趋势图和页面提交。

## 当前建议

不要直接把第 34 页的 `LT768_DMA_24bit_Block` 参数移植到当前字形路径，也不要仅凭启动 cache 探针通过就确认 BTE 几何正确。下一次实机实验应优先回答一个二选一问题：

```text
RIF 资源是 LT7680 标准外部字库格式？
还是项目自定义的 packed RGB565 tile？
```

只有在资源类型确认后，才能选择“外部字库文本引擎”或“自定义 RGB565 DMA/BTE”路径。两种路径的地址、stride、数据解释和完成等待方式不能混用。

## 结论

PDF 资料支持 LT7680 使用外部 Flash 和内部 SDRAM，也支持硬件 DMA、Canvas 和外部字库；但它没有提供当前 RIF `80x44` 预转置字形 tile 的直接范例。当前黑块/花屏最值得优先验证的是资源格式与 DMA/BTE 几何是否匹配，以及 DMA/BTE 完成后 Canvas/page 状态是否完整恢复。 
