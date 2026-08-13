# 无滚动仿真器布局工作台 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 K2000 布局仿真器改造成在 `1920×1080` 桌面视口内无页面滚动、可通过直接数值输入精确调参的工作台。

**Architecture:** 继续以 `sim/index.html` 的 `L` 对象为唯一布局状态。新增声明式参数元数据，将参数行、范围裁剪、`+/-` 微调、选中元素高亮和导出文本都收敛到这一元数据；保留 overlay 作为显示层，不再将画布拖拽作为编辑入口。DOM 初始化必须保留对 `sim/verify.js` 最小 mock 的防御。

**Tech Stack:** 原生 HTML、CSS、Canvas 2D、浏览器 Clipboard API、Node.js 无头验证。

## Global Constraints

- `1920×1080` 桌面视口不得产生页面滚动条；更小视口允许预览区或控制台内部滚动。
- 不修改 `main_display.h`、`main_display.c` 或任何固件文件。
- `L` 是唯一的可变布局模型；默认值继续逐项匹配 `main_display.h`。
- 每次参数变动都必须立即调用 `render()` 更新主屏与帧缓冲缩略图。
- `node sim/verify.js` 必须通过，且无头 mock 环境没有真实 overlay canvas、`navigator.clipboard` 或完整 DOM。
- 不新增依赖、持久化配置、文件导入或撤销栈。

---

### Task 1: 建立可测试的参数模型与无头冒烟测试

**Files:**
- Modify: `sim/index.html:160-625`
- Modify: `sim/verify.js:91-123`

**Interfaces:**
- Consumes: 已有的可变对象 `L`，已有 `ELEMENTS` 的 `id` 与 `box()`。
- Produces: `LAYOUT_PARAMS` 参数元数据数组；`setLayoutParam(id, value)`、`adjustLayoutParam(id, delta)`、`resetLayout()` 和 `exportLayoutText()` 函数。

- [ ] **Step 1: 在 `sim/verify.js` 添加失败断言，覆盖参数更新、裁剪、重置和导出**

在现有真实渲染冒烟块内，`eval(...)` 后追加：

```js
report("layout param API", typeof setLayoutParam === "function" &&
                           typeof adjustLayoutParam === "function");
setLayoutParam("topBandH", 40);
report("layout param set", L.topBandH === 40);
adjustLayoutParam("topBandH", -1000);
report("layout param clamp", L.topBandH === 8);
resetLayout();
report("layout param reset", L.topBandH === 24 && L.cursorH === 4);
report("layout export", /MAIN_DISPLAY_TOP_BAND_H 24u/.test(exportLayoutText()));
```

- [ ] **Step 2: 运行验证，确认新 API 断言失败**

Run: `node sim/verify.js`

Expected: FAIL，因为 `setLayoutParam`、`adjustLayoutParam` 与 `exportLayoutText` 尚未定义。

- [ ] **Step 3: 在 `sim/index.html` 定义布局参数元数据与更新 API**

在 `L` 常量后增加参数元数据。每项须包含 `id`、`label`、`group`、`unit`、`min`、`max`、`step`、`elementId`；参数顺序必须与设计文档的四组一致。

```js
const LAYOUT_PARAMS = [
  { id:"topBandH", label:"顶带高度", group:"垂直布局", unit:"px",
    min:8, max:200, step:1, elementId:"top_band" },
  { id:"sepH", label:"分隔线高度", group:"垂直布局", unit:"px",
    min:1, max:8, step:1, elementId:"separator" },
  { id:"readingH", label:"读数带高度", group:"垂直布局", unit:"px",
    min:24, max:200, step:1, elementId:"reading" },
  { id:"cursorGap", label:"光标间距", group:"光标", unit:"px",
    min:0, max:100, step:1, elementId:"cursor" },
  { id:"cursorH", label:"光标高度", group:"光标", unit:"px",
    min:1, max:40, step:1, elementId:"cursor" },
  { id:"unitPadX", label:"单位左边距", group:"顶部文本", unit:"px",
    min:0, max:400, step:1, elementId:"unit" },
  { id:"statusPadX", label:"状态右边距", group:"顶部文本", unit:"px",
    min:0, max:300, step:1, elementId:"status" },
  { id:"statusGap", label:"状态标签间距", group:"顶部文本", unit:"px",
    min:0, max:64, step:1, elementId:"status" },
  { id:"noDataSlots", label:"问号槽数", group:"无数据占位", unit:"slots",
    min:1, max:20, step:1, elementId:"nodata" },
  { id:"noDataDX", label:"问号水平偏移", group:"无数据占位", unit:"px",
    min:-480, max:480, step:1, elementId:"nodata" },
  { id:"noDataDY", label:"问号垂直偏移", group:"无数据占位", unit:"px",
    min:-160, max:160, step:1, elementId:"nodata" },
];

function layoutParam(id) { return LAYOUT_PARAMS.find(param => param.id === id); }
function setLayoutParam(id, value) {
  const param = layoutParam(id);
  if (!param || !Number.isFinite(value)) return;
  L[id] = clamp(Math.round(value), param.min, param.max);
  selId = param.elementId;
  render();
}
function adjustLayoutParam(id, delta) {
  const param = layoutParam(id);
  if (param) setLayoutParam(id, L[id] + delta * param.step);
}
```

将现有 `exportLayout()` 中的 `lines` 创建提取为 `exportLayoutText()`；原函数只负责将返回值写入可选 DOM 文本框和复制。

- [ ] **Step 4: 让拖拽路径复用参数 API，避免范围规则分叉**

将 `ELEMENTS` 的 `move()` 和 `applyDrag()` 内对 `L` 的直接写入改为调用 `setLayoutParam()` 或仅保留兼容的内部无渲染赋值辅助。拖拽本轮不再提供为 UI 操作，但保留时不得绕开参数范围。

- [ ] **Step 5: 运行无头验证，确认新增断言与原有一致性测试通过**

Run: `node sim/verify.js`

Expected: `sim/verify.js: ALL CHECKS PASS`。

- [ ] **Step 6: 提交参数模型和测试**

```bash
git add sim/index.html sim/verify.js
git commit -m "refactor(sim): centralize editable layout parameters"
```

### Task 2: 改造为 1920×1080 无滚动双栏工作台

**Files:**
- Modify: `sim/index.html:6-155`

**Interfaces:**
- Consumes: Task 1 的 `LAYOUT_PARAMS`、`setLayoutParam()`、`adjustLayoutParam()`。
- Produces: 紧凑双栏 DOM、`#layoutParams` 容器、`#copyStatus` 状态区、`buildLayoutControls()`。

- [ ] **Step 1: 将页面 CSS 替换为固定高度工作台样式**

保留 Canvas 像素渲染样式，新增或替换以下结构约束：

```css
html, body { height:100%; overflow:hidden; }
body { display:grid; grid-template-rows:auto minmax(0, 1fr); }
main { min-height:0; padding:12px; display:grid;
       grid-template-columns:minmax(0, 1fr) 430px; gap:14px; overflow:hidden; }
.preview { min-width:0; min-height:0; display:flex; align-items:flex-start; gap:10px;
            overflow:auto; }
.stage { width:720px; flex:0 0 720px; }
canvas#panel, canvas#overlay { width:720px; height:240px; }
.fb-preview { flex:0 0 94px; }
canvas#fb { width:80px !important; height:240px !important; margin:0 !important; }
.controls { min-width:0; max-width:none; overflow:auto; padding-right:2px; }
@media (max-width:1100px), (max-height:620px) {
  html, body { overflow:auto; }
  main { grid-template-columns:1fr; overflow:visible; }
  .controls { overflow:visible; }
}
```

确保 `header` 不超过约 `46px`，控件行高不超过约 `28px`，默认桌面视口的内容总高不超过 `768px`。

- [ ] **Step 2: 重新组织 HTML，移除占高的旧 devtools 区**

将 `.stage` 包入 `.preview`，把 `#fb` 放到 `.fb-preview` 内与主屏并排。删除以下旧界面节点：`#selInfo`、`#elemList`、`fitPanel()` 按钮、常驻 `#exportC` 文本框和冗长的拖拽提示。

在右栏保留场景控件并压缩成以下区块顺序：

```html
<section class="scene-controls">...</section>
<section class="layout-controls">
  <h2>布局参数</h2>
  <div id="layoutParams"></div>
</section>
<section class="layout-actions">
  <button type="button" onclick="exportLayout()">复制 C 宏</button>
  <button type="button" class="ghost" onclick="resetLayout()">重置布局</button>
  <span id="copyStatus" role="status"></span>
  <details><summary>查看导出内容</summary><textarea id="exportC" readonly></textarea></details>
</section>
```

- [ ] **Step 3: 实现参数控件生成和事件绑定**

在 `LAYOUT_PARAMS` 定义后增加按 `group` 保持首次出现顺序的渲染函数。每行使用 `data-param`，并确保按钮是 `type="button"`：

```js
function buildLayoutControls() {
  const host = document.getElementById("layoutParams");
  if (!host) return;
  const groups = new Map();
  LAYOUT_PARAMS.forEach(param => {
    if (!groups.has(param.group)) groups.set(param.group, []);
    groups.get(param.group).push(param);
  });
  host.innerHTML = "";
  groups.forEach((params, group) => {
    const section = document.createElement("section");
    section.className = "param-group";
    section.innerHTML = `<h3>${group}</h3>`;
    params.forEach(param => {
      const row = document.createElement("label");
      row.className = "param-row";
      row.dataset.param = param.id;
      row.innerHTML = `<span>${param.label}</span>` +
        `<button type="button" class="step" data-delta="-1" aria-label="减少 ${param.label}">−</button>` +
        `<input type="number" min="${param.min}" max="${param.max}" step="${param.step}" value="${L[param.id]}">` +
        `<button type="button" class="step" data-delta="1" aria-label="增加 ${param.label}">+</button>` +
        `<em>${param.unit}</em>`;
      row.querySelector("input").addEventListener("input", event =>
        setLayoutParam(param.id, Number(event.target.value)));
      row.querySelectorAll("button").forEach(button => button.addEventListener("click", () =>
        adjustLayoutParam(param.id, Number(button.dataset.delta))));
      section.appendChild(row);
    });
    host.appendChild(section);
  });
}
```

- [ ] **Step 4: 增加紧凑参数控件样式和选中状态**

添加 `.param-group`、`.param-row`、`.param-row.active`、`.param-row input[type=number]` 和 `.step` 样式：每行以 `grid-template-columns:minmax(0, 1fr) 24px 58px 24px 34px` 布局；输入文本右对齐；步进按钮固定 `24px` 方形；选中行以低饱和蓝色背景表示。删除未使用的 `.elem-row`、`#selInfo` 与 `.btnrow` 样式。

- [ ] **Step 5: 在脚本末尾构建参数面板后初始渲染**

将末尾调用从：

```js
render();
```

改为：

```js
buildLayoutControls();
render();
```

`buildLayoutControls()` 必须在 `#layoutParams` 不存在的无头环境下直接返回。

- [ ] **Step 6: 打开浏览器验证 1920×1080 工作台布局**

Run: `python3 -m http.server 8000 --directory .`

Open: `http://localhost:8000/sim/`

在浏览器开发者工具模拟 `1920×1080`，确认：页面无滚动条；主屏 1:1（960×320）与帧缓冲缩略图并排；全部 11 项参数可见；读数、状态、预设和导出操作仍可用。

- [ ] **Step 7: 提交无滚动工作台界面**

```bash
git add sim/index.html
git commit -m "feat(sim): add compact no-scroll layout workbench"
```

### Task 3: 同步参数面板、选中提示与安全导出

**Files:**
- Modify: `sim/index.html:447-625`
- Modify: `sim/verify.js:91-123`

**Interfaces:**
- Consumes: Task 1 参数元数据和 Task 2 的 `#layoutParams`、`#copyStatus`、`#exportC`。
- Produces: `syncLayoutControls()`、无拖拽依赖的 `drawOverlay()`、安全复制后的用户状态文本。

- [ ] **Step 1: 修改 `drawOverlay()`，只绘制当前选中元素**

删除 hover 命中、`elAt()`、`isHandle()`、canvas `mousedown/mousemove` 和 `window` 拖拽监听。保留 `overlay` 与 `octx` 的防御初始化，且仅在 `selId` 有对应元素时绘制一个彩色边框和短标签：

```js
function drawOverlay() {
  if (!octx) return;
  octx.clearRect(0, 0, UI_W, UI_H);
  const el = ELEMENTS.find(candidate => candidate.id === selId);
  if (!el) return;
  const b = el.box();
  octx.strokeStyle = el.color;
  octx.lineWidth = 2;
  octx.strokeRect(b.x + 0.5, b.y + 0.5, b.w - 1, b.h - 1);
}
```

- [ ] **Step 2: 实现参数控件同步函数并接入更新路径**

```js
function syncLayoutControls() {
  if (typeof document.querySelectorAll !== "function") return;
  document.querySelectorAll(".param-row").forEach(row => {
    const param = layoutParam(row.dataset.param);
    if (!param) return;
    const input = row.querySelector("input");
    if (input) input.value = L[param.id];
    row.classList.toggle("active", param.elementId === selId);
  });
}
```

在 `render()` 的结尾、`drawOverlay()` 前后调用 `syncLayoutControls()`；不得在每次渲染重建所有参数行，避免输入焦点被破坏。将 `setLayoutParam()` 的无效输入处理为恢复当前值而非写入 `NaN`。

- [ ] **Step 3: 实现 Clipboard API 优先的导出状态**

用下列语义替换 `exportLayout()`：

```js
function exportLayout() {
  const text = exportLayoutText();
  const area = document.getElementById("exportC");
  if (area) area.value = text;
  const status = document.getElementById("copyStatus");
  const done = message => { if (status) status.textContent = message; };
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(text).then(() => done("已复制"), () => legacyCopy(text, done));
  } else {
    legacyCopy(text, done);
  }
}
```

定义 `legacyCopy(text, done)`：有 `#exportC` 时选择文本并尝试 `document.execCommand("copy")`，成功写“已复制”，失败写“复制失败，请展开导出内容后手动复制”。无头环境不存在 DOM 节点时直接返回，不抛出异常。

- [ ] **Step 4: 扩展 `sim/verify.js` 的无头断言**

在调用 `exportLayoutText()` 后执行 `exportLayout()`，并断言脚本继续执行到最终 `ALL CHECKS PASS`。不要断言浏览器复制结果，因为无头 mock 不提供 Clipboard API。

- [ ] **Step 5: 运行完整验证并手工检查参数编辑**

Run: `node sim/verify.js`

Expected: `sim/verify.js: ALL CHECKS PASS`。

浏览器手工检查：输入 `topBandH=40` 后主屏读数起点下移；点击 `cursorH` 的 `+` 后下划线增高；输入 `-999` 到 `cursorGap` 后值裁剪为 `0`；点击重置后所有输入恢复默认值；点击复制后出现状态文本，展开区显示相同宏文本。

- [ ] **Step 6: 提交编辑同步和导出行为**

```bash
git add sim/index.html sim/verify.js
git commit -m "feat(sim): synchronize layout inputs and C export"
```

### Task 4: 更新文档并完成回归验收

**Files:**
- Modify: `sim/README.md:20-47`

**Interfaces:**
- Consumes: 最终参数面板交互、`exportLayout()` 与 `node sim/verify.js`。
- Produces: 面向使用者的紧凑参数工作台说明和验收记录。

- [ ] **Step 1: 替换 README 中旧的“devtools：可视化布局编辑”说明**

将拖拽/手柄/元素列表描述替换为以下事实：

```markdown
## 布局工作台

- 设计目标为 `1920×1080` 桌面视口无页面滚动；主屏以 1:1 像素显示（960×320），
  帧缓冲 320×960 缩略图并排预览。窗口更小时自动回退为单栏可滚动布局。
- “布局参数”按垂直布局、光标、顶部文本和无数据占位分组。每项可直接输入整数，
  或用 `−` / `+` 按钮逐像素（问号槽按格）微调。
- 修改参数后，选中的画布区域会高亮，主屏和帧缓冲预览立即更新；输入会被裁剪到可用范围。
- “复制 C 宏”复制 `main_display.h` 覆盖宏；展开“查看导出内容”可检查或手动复制文本。
- “重置布局”恢复与当前 `main_display.h` 一致的默认值。
```

- [ ] **Step 2: 运行最终无头验证**

Run: `node sim/verify.js`

Expected: `sim/verify.js: ALL CHECKS PASS`。

- [ ] **Step 3: 检查改动范围与提交历史**

Run: `git status --short && git diff --check && git log --oneline -5`

Expected: 仅 `sim/index.html`、`sim/verify.js`、`sim/README.md` 与本计划相关文档发生预期变动；`git diff --check` 无输出。

- [ ] **Step 4: 提交文档**

```bash
git add sim/README.md
git commit -m "docs(sim): document compact layout workbench"
```
