# 仿真器可显示内容 vs ODS 协议表核对（2026-08-14）

## 目的

核对 `sim/index.html` 中所有可显示内容是否都能在 `K2000_Panel_Protocol.ods`（RX/TX/Notes 三表）中找到来源。结论：**内容层面 100% 可溯源**；位映射存在一个已知的「约定 vs 字面列位」差异，需实物验证兜底。

## 方法

1. 将 ODS `content.xml` 用 ElementTree 逐格展开（正确处理 `table:number-columns-repeated`），得到 RX 表权威列位网格。
2. 枚举仿真器全部可显示内容（状态指示器、速率、占位文本、特殊态）。
3. 逐项对照 ODS，列出命中/未命中。

## 核对结果

### 状态指示器与速率 —— 全部命中

| 仿真器显示 | ODS 位置 |
|---|---|
| HOLD / TRIG / FAST / MED / SLOW | RX `0x08` |
| REM | RX `0x06` |
| REL / FILT / AUTO / ERR | RX `0x09` |
| Rate 占位 `Rate: ?` | RX `0x08` 速率组 |
| Range 占位 `Range ?` | 占位文本（协议层面量程信息蕴含于单位串，见 plan `2026-08-09-dmm-ui.md`） |

7 个状态指示器 + 3 个速率标签全部在 ODS RX 表有记录；仿真器与固件 `status_bar.c` 的 CORE 表一致（仿真器额外实现了 FILT，固件 core 表未含 FILT，见下）。

### 文本标签 —— 全部命中

ODS「Texts tags」段记录了 `0x02` flush、`0x04` POS、`0x0B` blink（参数 0x01/0x00）、`0x10` µ、`0x13` °、`0x18/0x1A/0x7F` 段控制；仿真器使用的 POS/blink 语义与之对应。

### 特殊态 —— ODS 未枚举（符合预期）

`OVERFLOW`、`----`（无读数）、`?`（无数据占位）是主机以 ASCII 文本下发的读数值，ODS 未枚举（AGENTS.md 已记录这一边界）。不属于协议缺失。

### 仿真器未实现、但 ODS 有记录的内容

`TALK`/`LSTN`/`SRQ`（0x06）、`SHIFT`/`TIMER`/`MATH`/`REAR`/`diode`/`beep`/`4W`/`Double Arrow`（0x07）、`*`/`BUFFER`/`STAT`（0x09）、`STEP`/`SCAN`/`CH1`/`CH2`（0x0A）、`CH3..CH10`（0x0E）。这些是里程碑-1 未实现的指示符，保留在 ODS 中作后续扩展依据。

## 已知差异：位映射约定 vs ODS 字面列位

### ODS 权威列位（逐格展开，表头 `0x80|0x40|0x20|0x10|0x08|0x04|0x02|0x01`）

```
0x06: 0x08=REM, 0x04=TALK, 0x02=LSTN, 0x01=SRQ      ← 0x80 列空
0x07: 0x80=SHIFT, 0x40=TIMER, 0x20=MATH, 0x10=REAR,
      0x08=diode, 0x04=beep, 0x02=4W, 0x01=Double Arrow
0x08: 0x10=HOLD, 0x08=TRIG, 0x04=FAST, 0x02=MED, 0x01=SLOW  ← 0x80/0x40/0x20 空
0x09: 0x40=REL, 0x20=FILT, 0x10=AUTO, 0x08=ERR, 0x04=*,
      0x02=BUFFER, 0x01=STAT                             ← 0x80 空
0x0A: 0x08=STEP, 0x04=SCAN, 0x02=CH1, 0x01=CH2           ← 0x80/0x40/0x20/0x10 空
0x0E: 0x80=CH3, 0x40=CH4, 0x20=CH5, 0x10=CH6, 0x08=CH7,
      0x04=CH8, 0x02=CH9, 0x01=CH10
```

### 固件/仿真器使用的约定

plan 文档 `2026-08-09-dmm-ui.md` 约定「每 TAG 的 bit 位映射 `0x80`=bit7 对应第一个指示符」，代码照此实现：

```
固件/仿真器: HOLD=0x08|0x80, TRIG=0x08|0x40, REM=0x06|0x80,
             REL=0x09|0x80, AUTO=0x09|0x20, ERR=0x09|0x10
```

### 差异说明

ODS 中每行**首个指示符并不落在 0x80 列**（0x80 列多留空），字面列位与 bit7=first 约定偏移。这是 AGENTS.md 已预警的已知情况：ODS 是「观测/实现记录」，列位不严谨，不能据此断言协议位映射。`status_bar.h` 注释已明确记录「bit7=first 是约定，非 ODS 字面列位」。

**处置**：此差异不阻塞里程碑-1；真机验收时以实物指示灯为准，若与 ODS/约定均不符再回改位表。

## 附：仿真器/固件 FILT 差异

- 仿真器 `CORE_TABLE` 含 FILT（0x09|0x40）。
- 固件 `status_bar.c` core 表 **未含** FILT（`STATUS_BAR_CORE_COUNT`=6）。
- 二者应保持同步：若里程碑后续需要 FILT 显示，需同步加入固件 core 表并加单测。

## 结论

- 可显示内容 100% 可从 ODS 找到，无缺失项。
- 位映射「约定 vs 字面列位」差异为已记录的历史情况，不影响内容覆盖率。
- ODS 中未实现指示符（TALK/LSTN/SRQ、0x07 组、0x0A 组、0x0E 组、0x09 余项）保留作扩展依据。

## 附：接收覆盖修复（2026-08-14）

原始解析器（`k2000_proto.c`）只能识别 0x0D 起始、6 个状态 TAG、POS、Blink 与 ASCII 字段；ODS「Texts tags」的 `0x10`(µ)、`0x13`(°)、`0x02`(flush)、`0x18/0x1A/0x7F`(segment 控制) 均 < 0x80，会作为读数字符被吞进 value（实测 `1.234` + `0x10` + `V` 变成 `.234\x10V`，µ 污染读数字符串）。

修复内容（firmware/src 与 CubeMX 树同步）：

- 解析器新增 `append_symbol`/`feed_lone_text_tag`/`control_tag_type`：
  - µ/°（内容符号）内联追加 UTF-8（`µ`=`\xC2\xB5`、`°`=`\xC2\xB0`）到当前字段值，**不关闭字段**，后续 `V` 仍在同字段，`reading_split` 才能正确拆出 `µV` 单位。
  - segment/flush（控制字节）先关闭当前字段（emit），再发 `K2000_EVT_SEGMENT`/`K2000_EVT_FLUSH` 事件并回 IDLE。
  - IDLE 状态遇到孤立文本标签发独立事件。
- 事件类型新增 `K2000_EVT_SYMBOL`/`K2000_EVT_SEGMENT`/`K2000_EVT_FLUSH`，事件结构新增 `ctrl` 原始字节。
- `ui_model` 新增 `apply_symbol`（µ/° 按 ODS 显式映射追加 UTF-8 到 unit）、`apply_segment`（记录 raw 段控制）、`apply_flush`（清 value/unit）。
- `main.c`（firmware 骨架 + CubeMX）分发三个新事件到 ui_model。
- 单测扩展：µ/° 内联不污染 value、segment/flush 关字段发事件、lone symbol、flush 独立事件；全部通过。

已知边界（本次未改、保持原语义）：字段只在收到下一个 tag 时 emit；`0x0D` 只重置不清空挂起字段，若主机每段以 `0x0D` 结尾且无后续 tag，该段读数字符串不会 emit（与 ODS「End of message when another tag is received」一致，真机确认后再定）。