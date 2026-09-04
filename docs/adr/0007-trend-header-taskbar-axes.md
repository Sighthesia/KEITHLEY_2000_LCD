# ADR-0007: 趋势区 header / 任务栏 / 纵轴

- 状态：已接受（2026-09-03）
- 前置：ADR-0006（底部只留绘图区）被本决策部分取代——绘图区让出 header / 任务栏 / Y 沟槽。

## 布局（底部 y192..319）

- header（y192 h26，BAR 底）：左侧 `Trend` 白字；右侧依次 `MAX v` / `AVG v` / `MIN v`（白）＋档位范围（绿， far right）。
- 绘图区（x96..960，y218..296，黑底）：5 条纵指示线（GRID），无横线、无标签悬浮。
- 任务栏（y296 h24，BAR 底）：纵线下方流逝时间（`10s 7.5s 5s 2.5s 0s`，青色），`0s` 在最新数据点（最右）。
- 左沟槽（x0..96，BAR 底）：resident 轴的最大/中/最小标签（青色右对齐）。

## 数据来源

- 档位范围由 resident 轴推导（`s_trend_axis_min/max/unit`），如 `±10V` / `100kΩ`（k/M 进制、DC/AC 后缀剥离、手动两位小数，无 float printf）；无数据时 `--`。
- MAX/AVG/MIN 沿用 `main_display_format_trend()` 的窗口统计文本；无数据时 `--`。
- 纵轴标签沿用 `main_display_format_linear_trend_labels()`（投影后的 resident 跨度）。
- 流逝时间由 `TREND_WINDOW_MS`（10s）五等分推导，与 sweep 槽位同源，刷新（背景重建）后重画。

## 渲染约束

- header 右块每串独占一步（单任务规则）；右对齐累计只在串之间垫间隔。
- 槽位擦除回补被盖住的纵指示线段（`trend_sweep_restore_verticals`）；回卷清屏后重画 5 线。
- Y 标签只在背景重建时绘制（同单位漂移不触发重建，标签即 resident 身份，天然稳定）。
- 空窗显示居中 `WAITING FOR DATA`；首个样本（单位变化）触发背景重建。

## 后续调整：Trend 徽标＋绘图顶端绿线

- `Trend` 改为仿档位徽标（绿底黑字，80px 宽 Badge，与行2档位标签同语言），不再是 header 上的白字。
- 绘图区顶端加全幅 2px 绿线（与行2分隔线同高同色）；纵指示线、顶部 Y 标签、擦除回补、回卷重画一律从线下起算，互不覆盖。

## 后续调整：绿线改 Trend 顶边＋统计抖动根治

- 绿线从绘图顶端搬到 Trend 顶边（y192 全幅 2px）；徽标与 header 文字整体下移 2px（线下内容带 y194..218），纵指示线/Y 标签/擦除回补回到绘图区基准。
- 统计抖动根因：两页在相邻帧各做一次背景重建，窗口滑动使 MAX/AVG/MIN 末位不同→翻页交替闪烁（与 sweep 双页教训同类）。
- 修复：header/任务栏/沟槽/标签/纵线复用 sweep 门控双页同写；提交时兄弟页共享有效位＋单位（其绘图区像素与记账不动，经 sweep 重扫收敛）。

## 纠正：背景必须整段双页写（部分双页更糟）

- 首版 jitter 修复只把 chrome（header/任务栏/标签/纵线）切双页、清屏留单页，导致：兄弟页旧字不清又被新字盖（Trend 行重叠、纵轴看似冻结）＋旧绘图区残留（背景闪烁），而共享有效位又阻止兄弟页重建自愈。
- 改为背景整段（清屏＋黑底＋全部 chrome）双页写：`invalidate_trend_pages()` 本来就清双页记账，两页从同一空白重启恒等；提交时兄弟页共享有效位＋单位。
- 规则：双页写要么整段、要么不写；半段双页必然制造分叉。

## Chrome 刷新层（统计刷新/防重叠/渐进 rescale）

- 症状：统计冻结（header 只在背景重建时画一次）＋长数值撞徽标＋同单位 rescale 整图黑闪。
- 修复：
  - 新增 `reading_only_render_trend_chrome()`：只重画 header 条＋沟槽条（双页，一串一步），统计变化 1Hz 跟随、gutter 重标立即；右块 x<200 钳位防撞徽标；跨页从清底重来防叠字。
  - 同单位 rescale 改渐进：保留绘图＋记账、sweep 重扫收敛，只置 gutter 重标；真单位切换仍全量黑底重建。
  - 背景提交与 chrome 提交都同步快照（cells＋ylabels＋tick），避免刚建完又触发。
- 规则：动态 chrome（header/沟槽）与静态 chrome（任务栏/纵线）分离；前者走快照＋节流，后者随背景一次建成。

## Chrome 补洞：顶边绿线也要重画

- chrome 刷新清 header 条却只重画徽标＋文字，顶边绿线被擦后永不回来。修复：chrome idx0 与背景同序（清底→绿线→徽标），两路径像素恒等。

## 周期性整图重绘的说明（非 bug）

- 单位切换（V↔mV、V↔Ω、demo 每 20 秒轮换档位）会清空 buffer（`trend_buffer_add` 按显示单位身份重置，旧单位曲线在新刻度下是谎言）＋黑底重建＋10 秒窗口缓慢重填——看到的是"灭隔一段时间重绘"。同单位内的换挡/量程漂移只渐进重扫，不黑闪。
- 如需同量纲（V↔mV）不断线，需 buffer 保留基值＋显示重定标，属于另一需求，未做。

## 500Hz 第三次回归＋TREND 让行

- `K2000_DEMO_INPUT_HZ` 又被改回 500（第三次）：每 2ms 一个读数帧，sweep 每轮拉满 48 槽预算（含慢速 MRWDP 回读），主循环单轮破百毫秒→读数冻结、趋势翻腾。改回 10Hz；判据：`input_hz=10` 且 `missed=0`，先查此再查渲染器。
- 结构补强：TREND 入口对新读数让行——快照 generation 合并、中断后经 SUFFIX→TREND 恢复（bg/chrome idx、sweep 游标、圆点状态全在静态量里续跑），33ms 节流保证趋势不饿死。换挡全量重建期间读数照常显示。
- 换挡全字体重绘本身是必要的（新单位全部文字都变），让行保证它不再挡住读数。

## 500Hz＋30fps 硬化（2026-09-04，需求反转）

- 过去"喂样率必须 10Hz"的结论被 bench 需求取代：现在要求 500Hz 输入 sustained＋30fps 显示，改为硬化管线而非回避速率。
- 探针：sweep 逐槽 MRWDP 回读探针默认关（`TREND_SWEEP_PROBE`，诊断时才开）——它一个人在 500Hz 下就能把单轮撑过 100ms。
- 文本单页画＋BTE 条带拷贝同步兄弟页（3 快操作代替 ~2x 逐墨段填充）；header 字形级差分（稳态只画变的 2-4 个字）；统计节流 1Hz。
- demo catch-up 预算 16→64（500Hz/30fps 下每轮欠约 17 个样）。
- TREND 入口读数让行（suspend/resume＋33ms 节流）。
- 实测（串口 PERF）：input_hz≈510、missed=0、fps 13~19、frame 35~57ms、填充 1400/s→200/s。距 30fps 仍差约 40%，瓶颈在逐操作 canvas 切页＋BTE/GE 忙等待，需驱动层批量优化（未做）。
- 根因找到（2026-09-04 实测）：`dm=492ms/73ops`——字形走了慢速 Flash DMA（~7ms/字，遗留 A/B 诊断开关 `READING_ONLY_DIRECT_DMA=1`），而非已验收的 SDRAM 缓存 BTE。切回缓存后 `dm≈120ms/100ops`（~1.2ms/字），fps 19~27、frame 11~14ms。单位轮换挡仍有约 1s 全量重建（fps 短暂到 8，设计内）。
- 旧板"静默卡死"一次（PC 全零，需复位恢复），60~90s soak 未复现；如再现，抓 wedge 前最后 PERF＋复现时长。

## 影响文件

- `firmware/src/main_display.h` ↔ `KEITHLEY_2000_LCD/Core/Inc/main_display.h`（三区几何）
- `KEITHLEY_2000_LCD/Core/Src/main.c`（helper＋背景17步＋stage标签＋擦除回补）
- `sim/index.html`、`sim/verify.js`
