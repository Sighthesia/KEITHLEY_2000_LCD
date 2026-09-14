# LT7680 渲染安全修复设计

日期：2026-09-13

## 目标

在不改变当前 RIF 渲染路径和资源格式判定的前提下，消除 DMA/BTE API 的可验证边界缺口，并保证 RIF cache 构建失败后不会留下错误的 Canvas 状态。

## 范围

- 为 Flash DMA tile 校验完整的目标矩形范围。
- 为 BTE blit 校验源 stride、完整源范围和目标范围。
- 为 RIF cache 构建统一失败清理，恢复进入前的 Canvas base/stride。
- 增加宿主测试覆盖非法参数和恢复行为可测试的逻辑。
- 不在本次修改中放宽 `rif_reader_find_glyph()` 的几何规则，不把 `80x44` 直接认定为有效格式。

## 设计

### DMA 目标范围

目标最后一个像素按完整 Canvas stride 计算：

```text
canvas_base + (dy + height - 1) * canvas_stride * 2
            + (dx + width_px) * 2
```

要求 `dx + width_px <= canvas_stride`，并要求整个范围不超过 LT7680 SDRAM 地址上限。

### BTE 源范围

要求 `src_stride >= width`，并按：

```text
src_addr + ((height - 1) * src_stride + width) * 2
```

检查源图最后一个像素不超过 SDRAM 上限。目标仍按面板宽高校验。

### Cache 失败清理

`rif_tile_cache_prepare()` 保存 Canvas 状态后，无论 Flash 读取、Canvas 设置、像素写入、CRC 或后续步骤在哪一步失败，都尝试恢复 `CVSSA/CVS_IMWTH`。原始错误优先返回；恢复失败时返回总线错误，并将 entry 标记为未就绪。

## 验证

- 运行 `firmware/tests/run_tests.sh`。
- 运行 `firmware/make` 的裸机逻辑构建（若工具链可用）。
- 运行 `node sim/verify.js`。
- 运行 `tools/tests/run_tests.sh`。
- 构建 `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`。
- 用 `diff -q` 确认共享逻辑在 `firmware/src/` 与 CubeMX 树一致。

## 后续实验

完成安全修复后，单独读取真实 RIF entry，确认 `80x44/stride=256` 的含义，再决定是否新增资源格式规则或切换到标准外部字库 API。
