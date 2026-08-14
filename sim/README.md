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
- 控件实时调整:读数数值/单位/速率、特殊态（正常白 / OVERFLOW 红 / 无读数灰）、
  启动无数据占位（'?' 槽 + `Range ?` + `Rate: ?`）、光标 POS + 250ms 闪烁、
  状态指示（HOLD/REM/REL/TRIG/AUTO/ERR/FILT）。
- 无连接（勾选“无数据 '?'”）时顶带左侧显示灰色占位 `Range ?` 与 `Rate: ?`；
  有连接时显示实际单位与速率（留空仍显示占位）。`FILT` 为开关灯：打开才点亮，
  关闭不显示，无连接时强制熄灭。
- 特殊态（无数据 `?` 槽、无读数 `----`、`OVERFLOW`）**复用读数的位置和大小**：
  均以 48×96 大数字字形渲染，`?` 占满右对齐的 7 个读数槽，`----`/`OVERFLOW`
  从读数槽起点绘制，与正常读数占据相同区域（大字库已扩展 `?` `R` `F` `L`）。
- **footer 规格行**：按单位自动推断测量类型，结合速率显示带宽/读数速度
  （DCV/欧姆 → `500 Read/s` 等；ACV/ACI → `300 Hz - 300 kHz 500 Read/s` 等），
  与固件 `main_display_footer_spec` 一致；位置可在“footer 规格”布局参数组调整。
- 预设一键切换常用场景（占位、正常读数、OVERFLOW、----、dBm、HOLD+光标）。

## 布局工作台

- 设计目标为 `1920×1080` 桌面视口无页面滚动；主屏以 1:1 像素显示（960×320），
  帧缓冲 320×960 缩略图并排预览。较窄窗口会先等比缩小左侧预览，保持参数栏常驻右侧；
  仅手机级宽度（小于 800px）才回退为单栏布局。
- 右侧控制栏独立滚动，展开场景控制或导出内容不会覆盖左侧预览。
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
