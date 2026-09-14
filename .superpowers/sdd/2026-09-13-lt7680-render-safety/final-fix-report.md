# Final Fix Report

日期：2026-09-14

## 修复内容

- 纯目的地址校验新增 `x + width_pixels <= stride_pixels`，并增加非零 `x` 的横向越界测试。
- CubeMX 树中的 Task 2 目的地址校验保持同一语义；没有尝试消除两棵 `lt7680_gfx.c` 的历史整体差异。
- RIF Canvas cleanup 仍会尝试恢复 base 和 stride。任一恢复失败统一返回 `LT7680_ERR_BUS`；如果主操作已经失败，则保留 primary error。两棵 `rif_tile_cache.c` 保持逐字一致。
- 最终复审 Minor 测试缺口已补齐：差异化验证 `primary=LT7680_ERR_TIMEOUT`、`restore=LT7680_ERR_PARAM` 时返回 primary；主操作成功而恢复返回 `LT7680_ERR_PARAM` 时返回归一化的 `LT7680_ERR_BUS`。原有恢复优先级测试保持不变。
- 四个 `docs/IMG_*.jpg` 删除是本计划之前的外部既有分支/工作区变更。本次未恢复、未修改，也未纳入本次安全修复提交。

## 验证结果

- `cd firmware && ./tests/run_tests.sh`：通过。
- `node sim/verify.js`：通过。
- `tools/tests/run_tests.sh`：通过。
- `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`：通过；若输出为 `ninja: no work to do`，含义是 Release 构建树已是最新，不是 fresh rebuild。
- `cd firmware && make`：源文件编译阶段通过，但最终链接仍有既有 `syscalls` 缺失符号 `_fstat`、`_isatty`、`_kill`、`_getpid`，因此不宣称全套构建通过。
- `diff -q firmware/src/rif_tile_cache.c KEITHLEY_2000_LCD/Core/Src/rif_tile_cache.c`：通过，两文件一致。
- `diff -q firmware/src/lt7680_gfx.c KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c`：仍不一致。两棵文件存在历史大规模差异；本次只保持 Task 2 相关目的地址校验语义一致，整体同步仍未完成。
- `git diff --check`：通过。

## 提交边界

本次提交仅包含本次安全修复涉及的源码、测试、验证报告和相关文档。未提交外部图片删除，以及 `.codegraph/`、`.embeddedskills/`、`tools/tests/test_spi_transport.py`。
