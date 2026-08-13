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

## 布局工作台

- 设计目标为 `1366×768` 桌面视口无页面滚动；主屏与帧缓冲缩略图并排显示。
- “布局参数”按垂直布局、光标、顶部文本和无数据占位分组。每项可直接输入整数，
  或用 `−` / `+` 按钮逐像素（问号槽按格）微调。
- 修改参数后，选中的画布区域会高亮，主屏和帧缓冲预览立即更新；输入会被裁剪到可用范围。
- “复制 C 宏”复制 `main_display.h` 覆盖宏；展开“查看导出内容”可检查或手动复制文本。
- “重置布局”恢复与当前 `main_display.h` 一致的默认值。

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