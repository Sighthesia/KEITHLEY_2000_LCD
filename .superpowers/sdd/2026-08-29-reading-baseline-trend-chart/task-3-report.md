# Task 3 Report: Static Trend Background and Axis Labels

## Status

Implemented Task 3 for the reading-only baseline trend renderer.

## Changes

- Removed `READING_ONLY_LEGACY` from `trend_restore_grid`, `trend_y_label_y`,
  `trend_draw_background`, and `trend_x_label_x`.
- Added resumable `reading_only_render_trend_background()` after the info-panel
  renderer.
- The helper paints the chart panel, plot background, divider, horizontal and
  vertical grid lines, Y labels, and X labels in separate resumable steps.
- The helper tracks render-page and unit changes, and only marks the page valid
  after every operation succeeds.
- Added valid-background Y-label-only refresh using the Y-axis gutter. The plot
  and X labels are left untouched on this path.
- Updated the Y-label snapshot only after the replacement labels finish.
- Replaced the Task 2 TREND stub with background rendering followed by PRESENT.
  IO failure invalidates the page background, records the failure flag, clears
  the transient IO error, and proceeds to PRESENT without aborting the frame.
- `trend_draw_column` remains `READING_ONLY_LEGACY`; no curve columns were added.

## Verification

Command:

```text
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

Result: passed. `KEITHLEY_2000_LCD.elf` linked successfully. Memory report:
RAM 15336 / 20480 bytes (74.88%), Flash 38304 / 65536 bytes (58.45%).

Additional checks: `git diff --check` passed. Self-review confirmed that the
TREND stage does not call `reading_only_abort_frame()` and does not render curve
columns.
