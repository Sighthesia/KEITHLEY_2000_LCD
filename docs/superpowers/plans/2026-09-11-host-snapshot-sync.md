# Host Snapshot Synchronization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 主显示区同步主机最新完整读数，不因单位轮播丢帧或等待趋势重建。

**Architecture:** 解析层产生完整主机快照，最新快照覆盖旧快照；主读数渲染使用 generation 边界。趋势和统计作为后台增强，不能阻塞主读数提交。

**Tech Stack:** STM32 C firmware, host-gcc tests, CMake/Ninja, SWD census.

## Global Constraints

- 主机是测量状态权威来源。
- value 与 unit 必须来自同一条主机记录。
- 不增加动态内存，不改变 UART 协议。
- 修改 `firmware/src` 逻辑镜像时同步 CubeMX 对应文件并运行测试。

### Task 1: Implement latest host snapshot path

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`
- Modify: `firmware/src/` 中与协议/读数模型对应的镜像文件（仅当接口实际共享）
- Test: `firmware/tests/test_reading_split.c` 或新增最小回放测试

**Requirements:**
- 删除主显示入口的单位门控丢帧。
- 解析结果按完整记录更新，value/unit 不可跨消息拼接。
- 新记录覆盖旧记录；不建立无限队列。
- 当前绘制作业发现 generation 变化时重新规划，不能继续提交旧快照的残片。
- 趋势、统计和背景重建不得阻止主读数阶段进入 PRESENT。

**Verification:**
- `firmware/tests/run_tests.sh`
- `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`
- `tools/swd_display_loop.sh`，确认 PRESENT 持续增加。

### Task 2: Add captured-stream regression coverage

**Files:**
- Modify: `firmware/tests/test_reading_split.c` or the smallest existing protocol test
- Add if needed: `firmware/tests/fixtures/host_reading_burst.txt`

**Requirements:**
- 覆盖 REV 行、占位符、VDC、mVDC、尾部 cursor dot 和连续 burst。
- 断言值与单位属于同一消息，且最新快照覆盖旧快照。

**Verification:**
- `cd firmware && ./tests/run_tests.sh`

### Task 3: Board verification and cleanup

**Files:**
- Modify: `AGENTS.md` only if measured behavior changes
- Modify: `tools/swd_display_loop.sh` only if required by the new counters

**Requirements:**
- 断开主机后编译/烧录，使用既定 OpenOCD halt-then-flash 流程。
- 重新连接主机验证主读数、单位后缀、功能标签、趋势空闲时的提交率。
- 移除临时 `[DEBUG-*]` 日志；保留有明确 SWD 诊断用途的单调计数器。

**Verification:**
- 宿主测试全部通过。
- Release ELF 构建通过。
- SWD 采样中 PRESENT 非零且持续增长。
