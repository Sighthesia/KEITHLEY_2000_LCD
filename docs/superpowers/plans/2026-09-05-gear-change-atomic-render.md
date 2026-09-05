# 换挡原子渲染 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use inline execution to implement this plan task-by-task.

**Goal:** 让启动和换挡时功能名称/徽标/Range/Rate 只以完整状态提交一次，同时保持读数实时并消除趋势重建造成的长卡顿。

**Architecture:** 在现有 `main.c` baseline renderer 中增加“待提交行快照”和稳定事件代次，换挡期间只渲染读数与后台趋势；完整行快照准备好后在隐藏页一次绘制并原子翻页。趋势背景和兄弟页克隆继续使用可恢复阶段与固定预算，不再以整段事务完成作为读数提交前置条件。

**Tech Stack:** STM32F103 bare-metal C, STM32Cube HAL, LT7680 GE/BTE, CMake/Ninja, OpenOCD, Node.js simulator.

## Global Constraints

- 不改变 K2000 协议字段含义、面板布局常量、字体资源或趋势坐标定义。
- 默认输入压力为 `K2000_DEMO_INPUT_HZ=500`，显示目标为 `DISPLAY_FRAME_PERIOD_MS=33`。
- 不增加固定延时掩盖事件不同步问题。
- 修改共享逻辑时保持 `firmware/src/` 与 CubeMX 工程对应文件同步；本次硬件渲染修改仅在 CubeMX `main.c`。

---

### Task 1: 建立换挡原子事务状态

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c:proto_on_event`, renderer state declarations

- [x] 使用现有换挡事务和页缓存状态区分新档位重建与普通状态更新；状态事件继续只更新模型。
- [x] 启动阶段保留首帧显示门控，没有完整首帧前不提交可见页。
- [x] 使用趋势单位/轴重建状态判断事务边界，不增加固定延时。

### Task 2: 让 STATUS/INFO 只消费稳定快照

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c:reading_only_render`, `reading_only_render_status_bar`, `reading_only_render_info_panel`

- [x] 换挡事务尚未完成时，STATUS/INFO 不再把中间行写入隐藏页。
- [x] 趋势完成后回到 STATUS/INFO，在同一隐藏页绘制新功能行与右侧状态后再提交。
- [x] 保留触发点单独更新能力。
- [x] 宿主测试和 `node sim/verify.js` 通过。

### Task 3: 限制换挡趋势工作预算

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c:READING_ONLY_TREND`, clone/present scheduling

- [x] 趋势列和背景保留可恢复游标。
- [x] 删除换挡完成路径中的同步 8 段整页克隆，避免 40ms 以上阻塞。
- [x] 单位切换期间保留旧可见页，完成新趋势和新行后才提交。
- [x] Release 构建通过，Flash 51056 B / 64 KiB，RAM 17240 B / 20 KiB。

### Task 4: 集成验证与文档

**Files:**
- Modify: `docs/adr/0007-trend-header-taskbar-axes.md`
- Test: `firmware/tests`, `sim/verify.js`, target serial/perf output

- [x] 宿主测试、仿真验证和 CubeMX Release 构建通过。
- [x] 已烧录并校验 `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`。
- [x] 目标板 PERF：`missed=0`、稳态 `fps=30~31`、`frame-ms=11~15`、`reading_errors=0`。
- [x] ADR 已记录事务边界、预算和实测结果。
