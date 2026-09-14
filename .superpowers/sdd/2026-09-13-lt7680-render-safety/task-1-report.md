# Task 1 Report

## 改动文件

- `firmware/src/lt7680_transfer_range.h`
- `firmware/src/lt7680_transfer_range.c`
- `firmware/tests/test_lt7680_transfer_range.c`
- `firmware/tests/run_tests.sh`

实现了纯 2D 源地址和目的地址范围校验，使用 64 位中间值计算最后一个字节，并拒绝零尺寸、`stride_pixels < width_pixels`、32 位地址溢出以及超过 `sdram_limit` 的传输。

## 测试命令和实际结果

命令：

```sh
cd firmware && ./tests/run_tests.sh
```

结果：PASS。所有宿主测试通过，其中新增 `PASS test_lt7680_transfer_range`，并覆盖边界、行尾填充、零尺寸、步长不足和溢出场景。

## 剩余疑问

Task brief 中要求失败的目的地址样例：

```c
lt7680_validate_2d_destination(0x00100000u, 320u, 0u, 892u,
                               128u, 68u, 0x01000000u)
```

按 brief 同时规定的目的地址公式，其结束地址约为 `0x00195E80`，小于 `0x01000000`，因此该调用应返回 true。测试按公式保留为成功断言，并增加了基地址 `0x00F70000` 的真实越界断言。需要需求方确认该样例是否还隐含面板高度或其他坐标上限约束；本 Task 未引入未定义的硬件坐标规则。
