# 显示渲染问题现状记录

日期：2026-09-13

## 当前目标

在 STM32F103C8T6 + LT7680A-R + ST7701S 替换显示板上恢复稳定的主读数显示，目标是无黑块、无花屏、无启动残留，并在正常读数变化时接近 30 fps。

## 当前已解决

- MCU 复位后左上角残缺大字已经消失。
- 完全断电后右侧亮色彩条已经消失。
- 启动阶段的可见 RIF DMA 字形探针已关闭：`K2000_RIF_DMA_PROBE=0`。
- 正常大字显示已恢复，不再使用错误的 `12x24` 小字 fallback。
- 外部 RIF 字形缓存启动构建和少量像素探针均已通过。
- 主机输入、固件宿主测试、仿真、资源工具测试和 Release 构建均通过。

## 仍存在的问题

### 黑块

黑块会随着主读数变化出现，表现为从右向左移动并逐渐变短，与新数字更新形成闪烁覆盖。该现象不是启动残留，仍属于运行期 framebuffer/BTE/页面一致性问题。

### 花屏和花点

顶部栏、底部趋势图仍出现花点/花屏；花点在字形边缘较明显，并且会随大字变化。将 RIF BTE 缓存路径切换为直接 Flash DMA 后花点减少，但黑块仍存在，说明：

- BTE cache 路径可能放大了 SDRAM 访问竞争；
- 直接 Flash DMA 不是黑块的根本修复；
- 页面翻转、字形擦除或隐藏页内容不完整仍是主要嫌疑。

### 刷新率

直接 Flash DMA A/B 路径下刷新率约 15--20 fps。恢复 SDRAM cache + BTE 后刷新率上升，但实机仍低于稳定 30 fps，且花屏/黑块未消失。

## 有效串口证据

诊断固件以 `9600 8N1` 抓取成功，日志保存在临时文件 `/tmp/k2000-diagnostic-retry.log`。关键内容：

```text
STATUS=0x50
RIF JEDEC=EF4017
RIF JEDEC raw=00EF4017
RIF sdram glyph cache=OK 26 tiles
RIF cache pixel probe=PASS
RIF cache canvas restore cvssa=00000000 stride=0140
RIF geom0 w=80 h=44 stride=0x0100
RIF glyph dir ready
RIF BTE renderer=ON
INIT-OK
[STALL] gap=15969 tick=15969 phase=04 item=00 col=00
[CLK] lost, restored
```

## 证据解释

- `cache=OK` 表示启动阶段的 DMA 操作返回成功并完成了 26 个缓存条目的构建流程，不等价于所有字形像素、源地址、stride 和 BTE 方向均已完整验收。
- `pixel probe=PASS` 只覆盖有限的缓存像素读回，不能证明运行期每个字形和每个目标页面都正确。
- 当前 RIF 实际返回的字形目录几何是 `80x44`、`256` 字节 stride；早期设计文档中的 `64x128`/`128x64` 描述不能直接作为当前资源的事实。
- `phase=04` 对应 `READING_ONLY_CLEAR`，启动后出现约 16 秒主循环停顿，说明清除阶段的 GE/BTE busy、Canvas 状态或时钟恢复路径仍有异常证据。
- `[CLK] lost, restored` 表明至少发生过 APB2/SPI 相关时钟状态异常；需确认它与 `phase=04` 停顿的先后关系。

## 当前配置

```text
K2000_RIF_DMA_PROBE=0
K2000_UART_LOG=0
K2000_PERF_LOG=0
READING_ONLY_DIRECT_DMA=0
RIF_BLIT_GAP_MS=1
LT7680_SPI_HW=0
K2000_READING_ONLY_BASELINE=1
```

当前正式代码不再保留 `READING_ONLY_DIRECT_DMA=1` 的 A/B 改动。未跟踪的 `.codegraph/`、`.embeddedskills/` 和 `tools/tests/test_spi_transport.py` 不属于本次显示问题记录。

## 已尝试方案及结果

| 方案 | 结果 |
| --- | --- |
| 启动前保持共享复位、最早写入 `REG[12h]=0x08` | 消除启动残留和彩条 |
| Raw 帧固定同步顶部/趋势大区域 | 黑块/花屏未可靠消失，刷新开销明显增加，后来移除 |
| Raw 绘制期间强制双页写 | 花点/页面竞争加重，不能作为正式方案 |
| Raw 绘制只写隐藏页 | 花点减少，但隐藏页内容同步不完整时会出现页面残留 |
| 禁用 RIF 大字，回退 `ui_draw_text()` | 大字消失并出现逐像素慢刷新，错误 fallback |
| 恢复外部 Flash DMA 大字 | 大字恢复，但约 15--20 fps，花点减少，黑块仍在 |
| 恢复 SDRAM cache + BTE 大字 | 刷新率上升，但黑块和花屏仍存在，字体边缘花点更明显 |
| BTE 字形间增加 `1 ms` 间隔 | 与历史记录一致，花点有所减少，但不能消除黑块 |

## 历史根因记录

- `c4b400a`：连续 BTE 字形搬运与显示扫描竞争会产生字体边缘单像素花点；建议字形间隔 `1 ms`。
- `3b0c04e`：运行期应只写隐藏页，使用真实脏区同步，不使用双页穿透写或整页复制。
- `28eaea1`：复制 Trend 像素后必须同步 Trend 的 `y0/y1/occupied` 等软件缓存，否则会产生旧曲线残影。
- `e480358`：字形替换时只擦除消失位置，不应提前擦除有新字形替换的位置，否则会暴露黑色空窗。
- `faa4e01`：字形游标必须按 UI 字距推进，不能直接使用预转置 tile 的 framebuffer 宽度。
- `4d9bb47`：`READING_ONLY_DIRECT_DMA=1` 会使帧率落到约 `19--27 fps`；缓存 BTE 才是性能路径。
- `9390e5b`：曾记录部分 BTE 页面复制导致物理左侧黑闪，说明转置 framebuffer 下部分矩形同步不能只凭逻辑坐标判断正确。

## 当前未决假设

1. Raw 专用路径没有完整复用正常 renderer 的 page cache、脏区和 Trend 软件缓存状态，翻页时可能暴露旧页面内容。
2. RIF BTE cache 的 `source address`、`source stride=128 pixels`、tile 方向和缓存实际排布可能不一致；有限像素探针不足以证明整字正确。
3. `lt7680_flash_dma_tile_to_canvas()`、`lt7680_gfx_blit()` 或 `lt7680_gfx_fill_rect()` 后 Canvas base/width、DMA/BTE 状态没有完全恢复，导致后续 `READING_ONLY_CLEAR` 停顿或错误写入。
4. LT7680 SDRAM 初始化容量/refresh interval、MCLK/PCLK 或 APB2/SPI 时钟恢复仍可能造成内部 SDRAM 访问异常。
5. 当前系统没有独立 VSYNC 输入/中断证据；仅靠固定 `1 ms` 间隔不能保证大块 BTE 与显示扫描完全错开。

## 下一步验证顺序

1. 保持 `K2000_UART_LOG=1`、`K2000_PERF_LOG=1` 的临时诊断版本，重新获取 `PERF`、`bte-hit/miss`、`dm`、`stg` 和 `[STALL]` 的时间关系；完成后恢复静默。
2. 在固定纯色 framebuffer 下测试 `fill_rect`，确认没有 BTE/DMA/字形参与时是否仍出现花屏。
3. 用固定 `32x32` RGB565 图案单独验证 Canvas page、BTE source/destination address、stride 和 page flip。
4. 单独验证一个 `8` 字形的 cache tile，检查完整 tile 的方向、边缘和背景，不使用动态读数。
5. 确认每次 DMA/BTE 后都恢复 Canvas base、Canvas stride、Flash controller 状态，并分别等待 DMA/BTE/2D busy 清零。
6. 仅在上述固定图案和单字形实验通过后，重新接入 Raw 动态读数和 Trend。

## 结论

当前问题尚未解决。启动复位残留已经解决，RIF 缓存构建也有启动日志证明成功，但运行期黑块和花屏仍然存在，且与字形更新同步。现阶段不应继续通过调整刷新周期、增加任意延时或切换完整 UI 框架来猜测修复；应先完成 LT7680 固定图案、BTE 几何和单字形的分层硬件验证。
