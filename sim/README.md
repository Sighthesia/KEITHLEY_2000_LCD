# K2000 TFT Reading-Page 布局仿真器

在电脑浏览器里逐像素预览固件 `main_display_format` / `ui` 渲染管线的输出,
无需烧录。数据与逻辑全部与 `firmware/src` 同源。

## 用法

```bash
# 1. 打开仿真器（直接双击或用静态服务器）
#    例如: python3 -m http.server 8000  ->  http://localhost:8000/sim/
open sim/index.html

# 2. （重新）生成字体数据（仅当 firmware/src 字体位图变更时）
python3 tools/make_sim_font.py firmware/src sim/font_data.js

# 3. 无头一致性验证（需 node）
node sim/verify.js
```

## 功能

- 主画布 = 可见屏 960×320（横屏 UI 空间,`panel_transform` 转置后即物理面板所见）。
- 右侧小画布 = 帧缓冲 320×960（`fb_x=ui_y, fb_y=ui_x` 转置预览）。
- 控件实时调整:读数数值/单位、特殊态（正常白 / OVERFLOW 红 / 无读数灰）、
  启动无数据 '?' 占位、光标 POS + 250ms 闪烁、六路状态指示（HOLD/REM/REL/TRIG/AUTO/ERR）。
- 预设一键切换常用场景（占位、正常读数、OVERFLOW、----、dBm、HOLD+光标）。

## devtools：可视化布局编辑

主画布上方叠加了 devtools 交互层，可像浏览器开发者工具一样直接拖动/缩放
各 UI 元素，实时重排整幅画面，并把结果导出为可粘贴进 `main_display.h` 的 C 宏：

- 可编辑元素（点选后出现彩色边框 + 标签）：
  - **顶带** `MAIN_DISPLAY_TOP_BAND_H`：拖动改高，右下角手柄缩放。
  - **分隔线** `SEP_Y`：上下拖动。
  - **读数带** `READING_H`：上下拖动移动顶边，右下角手柄改高。
  - **光标下划线** `MAIN_DISPLAY_CURSOR_GAP / _H`：上下拖动改间距，手柄改高。
  - **无数据 '?' 槽**：自由拖动（`noDataDX / noDataDY` 偏移）。
  - **单位文本**：左右拖动（`unitPadX`）。
  - **状态块**：左右拖动（`statusPadX`，右缘间距）。
- 操作方式：
  - 鼠标悬停：灰色边框 + 标签预览；单击选中（彩色框 + 坐标读取）。
  - 选中后拖动元素移动；有元素的右下角出现手柄，拖动它缩放。
  - 下方列表也可点选元素；`导出 C 宏` 生成 `#define` 覆盖文本（自动复制），
    `重置布局` 恢复默认值，`缩放到容器` 适配窗口宽度。
- 所有改动即时作用到可见屏、帧缓冲预览与导出文本；默认值与 `main_display.h`
  一致，未做任何改动时导出内容即当前固件默认布局。

## 与固件的一致性（verify.js 逐项断言）

| 项 | 来源 |
| --- | --- |
| 布局常量 960×320 / 顶带 24 / 读数带 96 / 分隔线 / 光标槽 | `main_display.h` |
| 右对齐算法 `main_display_layout_value` / `main_display_cursor_x` | `test_main_display.c` 断言 |
| 特殊态颜色 0xFFFF / 0xF800 / 0xC618 | `main_display_special_color` |
| status 右对齐求和（标签宽 + 间隙） | `main_display_format` |
| 数字字形墨点数 28 字逐一相等 | `font_digits.c` |
| 文本字形墨点抽样 | `font_text.c` |
| 完整 render() 冒烟（墨点量级 + 垂直分布） | `main.c` `reading_scene_render` |

## 文件

- `sim/index.html` — 仿真器（单文件,加载 `font_data.js`）。
- `sim/font_data.js` — 由 `tools/make_sim_font.py` 从 C 位图生成（勿手改）。
- `sim/verify.js` — 无头一致性验证（node sim/verify.js）。
- `tools/make_sim_font.py` — 提取脚本。