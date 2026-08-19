# RIF MISO 诊断记录

日期：2026-08-19

状态：硬件访问未验证通过，U5 保持只读

## 现象

恢复 RIF 启动读取后，LT7680 控制接口本身正常，但外挂 U5 的读回数据全部
为 `0xFF`：

```text
STATUS=0x50
RIF header status=0x00 attempted=01 raw=FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
RIF JEDEC=FFFFFF
RIF JEDEC raw=FFFFFFFF
RIF FIFO probe attempted=01 full=00 status_err=00 last_status=0x80
RIF unavailable: invalid header
```

同时观察到：

- `SPIMSR=0xA0`，没有报告 FIFO 溢出；
- LT7680 普通寄存器读写成功；
- `SPIMCR2` 在事务结束后回到 `0x0C`，这是清理后的 idle 状态，不能证明事务期间的模式值；
- `SFL_CTRL` 写入 `0x00` 后读回 `0x40`，该寄存器至少包含硬件强制位或只读位；
- 改用 SPI mode 0（`SPIMCR2=0x1C`）后，硬件仍返回 `0xFF`，因此 mode 3 不是唯一原因。

## 示波器证据

此前对 Flash 总线的放大波形显示：

- CS 全程保持低电平；
- MISO 在事务前段处于高阻或弱偏置状态；
- 约 `14.8 us` 处，MISO 从约 `1.8 V` 快速下降到 `0 V`；
- 之后 MISO 紧贴 GND，表现为强驱动低电平。

这证明事务后段至少出现了外部输出驱动动作，不能简单归因于 MISO 永久断线。
但“拉低”只能证明当前 bit 或阶段存在低电平，不能单独证明返回字节为 `0x00`，
也不能证明 LT7680 已正确把该数据放入 RX FIFO。

## 当前判断

最可能的故障范围按优先级排列如下：

1. LT7680 SPI Master 的 Flash 片选选择或 Flash bank 配置不匹配；
2. LT7680 的传输长度、命令阶段和数据阶段边界与 W25Q 的标准 SPI 时序不匹配；
3. LT7680 RX FIFO 的取数顺序与发送字节的对应关系错误；
4. U5 的 CS、SCK、MOSI、MISO 或供电存在连接/电气问题；
5. SPI mode 仍不匹配，但 mode 0 已经按计划验证过一次且没有改善。

Dual/Quad SPI 目前不是首要假设。当前固件发送的是标准 SPI 的 `0x9F` 和 `0x03`，
没有发送 Quad Read 命令，也没有修改 Flash QE 状态。

## 无示波器排查流程

所有步骤都只读 U5，不执行 `0x06`、`0x20`、`0xD8`、`0x02` 或任何写入/擦除命令。

### 1. 固定单一硬件测试

临时让启动流程只执行一次 JEDEC 读取，不执行 RIF header、目录扫描或显示渲染。
每次只改变一个 SPI Master 参数，并打印：

```text
mode, divisor, SFL_CTRL readback, SPIMCR2 before, SPIMSR before/after, RX raw[4]
```

目标是把“总线无响应”和“RIF 解析失败”分离。成功标准只有：

```text
RX raw = 00 EF 40 17
JEDEC = EF4017
```

### 2. 记录事务期间的寄存器值

当前日志只记录了事务结束后的 `SPIMCR2=0x0C`，信息不足。应在：

1. `flash_begin()` 完成后；
2. 写入 `0x9F` 前；
3. 写入三个 dummy byte 后；
4. 读取 RX FIFO 前；
5. 清理 SPI Master 后；

分别读取并打印 `B7`、`B9`、`BA`、`BB`。不要把清理后的 `0x0C` 当作活动配置。

### 3. 比较 mode 0 和 mode 3

分别使用：

```text
mode 0: SPIMCR2 = 0x1C
mode 3: SPIMCR2 = 0x1F
```

其余参数完全不变。每种模式重复 5 次 JEDEC 读取。

- 始终 `FF FF FF`：优先排查 CS/Flash 选择、RX FIFO 和硬件连接；
- 两种模式均出现稳定的非 `FF` 数据：再确认数据位顺序和 dummy 字节位置；
- 只有一种模式出现 `EF 40 17`：保留该模式，并更新硬件验收记录。

### 4. 比较分频值

保持 mode 0，只测试保守分频，不同时修改其他寄存器。建议顺序：

```text
0x0F -> 0x1F -> 0x07
```

每个值重复 5 次。分频变化只能用于判断时序裕量，不能替代 CS 或命令流程验证。

### 5. 检查 RX FIFO 位置和顺序

对 `0x9F + 3 dummy`，保留四个原始 RX 字节，逐个打印，不只打印解析后的 ID。
确认代码是否把：

```text
RX[0] 作为 command 阶段返回值丢弃
RX[1] 作为 manufacturer ID
RX[2] 作为 memory type
RX[3] 作为 capacity
```

如果原始 FIFO 是 `FF EF 40 17` 或 `00 EF 40 17`，Flash 实际已经正确响应，
问题只在 FIFO 偏移或清空顺序。

### 6. 读取已知地址的短数据

只有在 JEDEC 成功后才测试 `0x03`：

```text
03 00 00 00 FF FF FF FF
```

先读取 4 字节，打印所有 RX 原始值，再读取 RIF header。不要直接从长 header 读取
开始排查，因为长事务会混入 FIFO 深度、传输长度和数据阶段问题。

### 7. 无仪器的硬件静态检查

断电后检查并记录：

- U5 VCC 与 GND 是否连续、是否有正确电压；
- U5 `CS#` 是否连接到 LT7680 的 Flash CS，而不是 STM32 的普通 GPIO；
- U5 `CLK`、`DI/MOSI`、`DO/MISO` 是否对应 LT7680 SPI Master 端；
- U5 `WP#`、`HOLD#` 是否被拉高；
- MISO 是否存在明显短接到 GND 或其他输出端；
- U5 型号/容量是否与当前 RIF 烧录位置一致。

本板网表要求 U5 挂在 LT7680 SPI 上，不经过 STM32。静态检查不能证明时序正确，
但可以排除片选接错、供电缺失和 MISO 硬短路。

## 不应采取的操作

- 不要向 U5 发送写使能、页编程、擦除或状态寄存器修改命令；
- 不要把 `SFL_CTRL` 的写回 `0x40` 强行覆盖成假定值而没有同步波形或数据证据；
- 不要启用 DMA/硬件字库模式来绕过当前 JEDEC 失败；
- 不要把 `0xFF` header 当作有效 RIF，也不要进入字形瓦片缓存构建；
- 不要用 LT7680 显存回读结果判断 U5 总线，当前 SPI 模式下显存回读本身不可靠。

## 验收标准

无示波器情况下，至少应获得以下稳定结果后，才恢复 `s_rif_ready` 的有效渲染路径：

```text
RIF JEDEC raw=00EF4017  (或等价的首字节丢弃布局)
RIF JEDEC=EF4017
RIF header status=0x00
RIF external digits ready
```

在此之前，继续使用内部字体回退是预期行为；当前 `PERF` 的 15–17 fps 与 RIF
失败无关，显示仍走内部字体的逐块 GE/Flash 路径。
