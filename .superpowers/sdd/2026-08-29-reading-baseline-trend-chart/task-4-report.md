# Task 4 Report: Incremental Trend Columns

## Status

Completed Task 4 for the reading-only baseline trend chart.

## Changes

- Reworked `trend_draw_column` into a non-legacy `bool` function.
- Replaced the legacy floating-point Y calculation with `main_display_trend_plot_y`.
- Added difference checks, previous-column erase handling, grid restoration, and trend-column performance accounting.
- Made `ui_draw_line` record reading-only LT7680 I/O failures in the same way as `ui_fill_rect`.
- Projected trend data after the background succeeds, then rendered columns incrementally.
- The TREND stage skips unchanged columns in the same call and yields after a column that issued GE, resuming with `s_reading_only_trend_column`.
- No-data frames skip column rendering; column I/O errors proceed to PRESENT without aborting the already-rendered reading.

## Verification

- `firmware/tests/run_tests.sh`: PASS, all host tests passed.
- `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`: PASS.
- `git diff --check`: PASS.

## Commit

`0e90758 feat: incremental trend columns on the reading baseline`

## Review Fix

The trend-column cache comparison now skips only when both projected Y
coordinates are exactly unchanged. A 1px projection change therefore erases
and redraws the column as required.

### Verification

Command: `(cd firmware && ./tests/run_tests.sh)`

Output: all host tests passed, including `PASS test_trend_buffer`,
`PASS test_main_display`, and `PASS test_render_scheduler`.

Command: `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`

Output:

```text
[1/2] Building C object CMakeFiles/KEITHLEY_2000_LCD.dir/Core/Src/main.c.obj
[2/2] Linking C executable KEITHLEY_2000_LCD.elf
RAM: 16296 B / 20 KB (79.57%)
FLASH: 39112 B / 64 KB (59.68%)
```

`git diff --check`: PASS.
