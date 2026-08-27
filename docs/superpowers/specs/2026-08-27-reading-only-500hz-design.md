# 500Hz Main Reading Baseline

## Goal

Establish a reliable on-target rendering baseline that accepts 500 Hz demo
readings, coalesces them to the newest value, and commits a main-reading-only
screen at 30 Hz without long stalls or a stuck frame.

## Scope

- Keep the existing 500 Hz demo protocol feed and normal K2000 parsing path.
- Render only the main reading value and unit at the current reading location.
- Compose every runtime update on one canvas page and present it directly.
- Clear the complete reading area before drawing the latest value so no prior
  glyph, suffix, diff-cache, or page-sync state can survive into the frame.
- Schedule no trend, status bar, information panel, blink, or hidden-page
  synchronization work in this baseline.

## Scheduling

- Input remains `K2000_DEMO_INPUT_HZ=500`.
- A reading event only marks the newest snapshot dirty; it never queues a
  separate render job.
- A render may start once every `DISPLAY_FRAME_PERIOD_MS=33` milliseconds.
- A successfully completed render increments `display_commits`; failed LT7680
  operations abandon the current frame and leave the newest snapshot dirty for
  the next period. No failure may retain an in-progress frame indefinitely.

## Rendering

- The canvas page is selected explicitly for every frame.
- The reading band is filled black through the geometry engine.
- The RIF SDRAM glyph cache is used when available. Its BTE blits draw only
  the active canvas page.
- The display page is presented only after all current reading glyphs finish.
- The baseline does not attempt page parity. A future double-buffered design
  must be rebuilt on a bounded command-state interface rather than restoring
  the former implicit sync path.

## Acceptance

- The screen contains only the main reading and its unit/suffix.
- With the 500 Hz demo, `input_hz >= 450`, `reading_frames >= 28`, and
  `display_commits >= 28` over a one-second steady-state window.
- `missed=0`; no sampled main-loop gap or completed frame exceeds 100 ms.
- The target runs for 60 seconds without a stalled renderer or a partial main
  reading.
