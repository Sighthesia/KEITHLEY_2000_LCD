# Host Snapshot Synchronization Design

## Goal

让主显示区始终快速呈现主机最新的完整显示记录，并使趋势/统计渲染不能阻塞主读数。

## Design

主机输入解析后写入一个 latest snapshot（value、unit、function、status、generation），新记录覆盖旧记录，不排队历史记录。主读数渲染以 snapshot generation 为边界；值和单位必须来自同一条记录，旧绘制作业被新 generation 取消或完整替换。

移除主显示路径上的单位滞后丢帧。特殊读数仍按主机内容显示。趋势和统计从主显示提交路径解耦；同族单位换算和趋势轴重建作为后续独立阶段，不得阻塞主读数提交。

## Verification

使用真实主机片段回放，验证 `0 0.011933 VDC` 与 `008.7481mVDC.` 的值/单位成组解析，连续输入时只保留最新快照，且渲染不会出现 `VD` 半截单位。运行宿主测试、CubeMX Release 构建，并用 SWD 计数器确认提交率不被趋势阶段拖低。
