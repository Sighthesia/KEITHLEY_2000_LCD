# Keithley 2000 Industrial Trend UI Design

Date: 2026-08-14
Status: Approved in design review

## Goal

Replace the existing reading scene with a dense, dark industrial instrument UI
for the 960x320 landscape logical display. The scene combines the live Keithley
2000 reading with authentic annunciators and a continuously scrolling trend
chart. The browser simulator, offline firmware skeleton, and CubeMX target must
remain behaviorally synchronized.

The design uses Keithley 2000 terminology and measured protocol evidence. It
must not present DMM6500 concepts such as DIGITIZE, defbuffer1, No Script, or
DCCPL as Keithley 2000 state.

## Product Decisions

| Topic | Decision |
| --- | --- |
| Delivery | Replace scene 0 in simulator, offline firmware, and CubeMX firmware |
| Visual style | Black industrial background, high contrast, neon green reading |
| Terminology | Original Keithley labels: REM, TALK, LSTN, SRQ, TRIG, FILT, REL, MATH |
| Authority | Host protocol is authoritative; unavailable values are shown as unknown or reduced forms |
| Trend window | Save and display the most recent 10 seconds |
| X axis | 0.00s, 2.50s, 5.00s, 7.50s, 10.00s |
| Y axis | Automatic range from visible samples with 10% padding |
| Rate display | FAST=500 Read/s, MED=50 Read/s, SLOW=5 Read/s |
| Protocol bits | Migrate status indicators to the literal ODS bit values, then verify on hardware |
| Rendering | Layered native LT7680 renderer; no browser-only SVG/Recharts dependency |

## Screen Layout

All positions are authored in the existing 960x320 UI logical space and pass
through the pure transpose framebuffer transform.

```text
y=0..23    Status: REM TALK LSTN SRQ | BUFFER | GPIB | CONT TRIG
y=24..47   Function: DC VOLTAGE                              Zin
y=48..143  Reading: +03.68900 V
y=144..167 Parameters: Range | Filter | Rate
y=168..191 GRAPH  REAL-TIME TREND | pagination | FILT REL MATH
y=192..319 Trend plot, Y labels at left, X labels at bottom
```

### Visual Language

- Background: RGB565 black with two neutral-grey elevations for the status and
  graph divider bars.
- Primary active color: neon green equivalent of `#00FF33` for valid readings,
  active measurement state, and trend data.
- Secondary active color: cyan for the graph title and axes.
- Main labels: white; inactive indicators and unavailable metadata: dark grey.
- Parameter controls use compact 4px-radius rectangles, not oversized pills.
  On target, the radius is rendered with clipped corner pixels around a filled
  rectangle; it does not require a new LT7680 rounded-rectangle primitive.
- Grid lines are subtle 1px dashed dark grey.
- The trend plot's X and Y axes sit in L-shaped deep-grey cells matching the
  reading area's info-panel cells (`MAIN_DISPLAY_COLOR_BAR`): a full-height
  left strip (x0..95) for the Y labels and a bottom strip (y296..319, plot
  width) for the X labels. Y labels are right-aligned inside the strip 4px from
  the plot edge; X labels stay centred on the grid lines.
- The trend glow is deterministic and inexpensive: a bright 1px center line
  with adjacent darker green lines. No alpha blur is required on the LT7680.
- Text remains on the existing generated monospace bitmap fonts. The large
  value keeps the host-provided significant digits and trailing zeros. Its unit
  shares the baseline in the smaller text font.
- The five graph pagination dots are decorative in this single-scene release;
  the fifth dot is white and the other four are muted grey.

### Reading and Metadata

The function heading is inferred from the received unit and status data when
possible:

- `DC VOLTAGE`, `AC VOLTAGE`
- `DC CURRENT`, `AC CURRENT`
- `2W OHM`, `4W OHM`
- `FREQUENCY`, `PERIOD`, `TEMPERATURE`
- `MEASUREMENT` when the function cannot be identified reliably

The renderer never pads a reading to synthetic 6.5-digit precision. It displays
the exact numeric text sent by the host.

Input impedance follows the Keithley 2000 specification only when function and
range are both known. The 100mV, 1V, and 10V DC ranges show `>10GΩ`; the 100V
and 1000V DC ranges show `10MΩ`; AC voltage shows `1MΩ`. The existing dedicated
ohm symbol glyph renders `Ω`; these labels are not reduced to ASCII `ohm`. If
the range is not available from the observed panel protocol, target firmware
shows `Zin: --`.
The simulator's complete design preset may explicitly configure a 10V range and
therefore show `>10GΩ`.

## Status and Parameter Semantics

### Protocol-Driven Indicators

The implementation migrates from the current `bit7 = first indicator`
assumption to the literal ODS column values:

| Tag | Bit mapping used by the new UI |
| --- | --- |
| `0x06` | `0x08 REM`, `0x04 TALK`, `0x02 LSTN`, `0x01 SRQ` |
| `0x07` | `0x20 MATH`; retain the remaining documented bits in the model |
| `0x08` | `0x10 HOLD`, `0x08 TRIG`, `0x04 FAST`, `0x02 MED`, `0x01 SLOW` |
| `0x09` | `0x40 REL`, `0x20 FILT`, `0x10 AUTO`, `0x08 ERR`, `0x02 BUFFER` |

This is a behavior migration, not an established hardware fact. Target
acceptance must inject each status byte independently and compare the TFT state
with the physical instrument annunciators. Any hardware disagreement is fixed
in the table, not hidden behind dual mappings.

### Display Rules

- `REM`, `TALK`, `LSTN`, `SRQ`, `TRIG`, `FILT`, `REL`, `MATH`, and `AUTO` are
  driven directly by protocol bits.
- `CONT` means the TFT's local trend acquisition is continuously active. It is
  not presented as an original Keithley annunciator.
- The third parameter control displays `Rate: 500 Read/s`, `Rate: 50 Read/s`,
  or `Rate: 5 Read/s`. NPLC is not shown in this UI.
- If no explicit range value is available, show `Range: AUTO` or
  `Range: MANUAL` from the AUTO bit. Do not invent `10V`.
- If filter type and count are unavailable, show `Filter: ON` or `Filter: OFF`.
  Do not invent `MOVING 10` on target hardware.
- Show `GPIB: --` when the address is unavailable. The simulator may set 16 in
  its complete design preset.
- Show `BUFFER: RECALL` while the BUFFER bit is set. Otherwise show
  `BUFFER: IDLE (1024 MAX)`, where 1024 is explicitly a capacity, not a live
  sample count.

## Trend Data Model

### Sampling

`trend_buffer` is a pure C module with no HAL or display dependency. It stores
500 20ms buckets, covering 10 seconds at 50Hz. Each bucket keeps minimum and
maximum values so a short excursion remains visible when the host produces up
to 500 readings per second. The expected storage cost is about 4KB plus small
ring metadata and an occupancy bitset. Bucket extrema use 32-bit `float` only
for graph projection; the exact host reading remains the original string in
`ui_model`. The decimal parser is bounded and locale-independent and does not
pull `strtof`, formatted I/O, or heap allocation into the target image.

Every valid numeric reading is parsed once and normalized to a base physical
unit before insertion. Prefix changes such as mV to V do not clear the history
when they represent the same physical quantity. A function or physical-dimension
change clears the history. `OVERFLOW`, `----`, startup placeholders, and invalid
numeric text are not sampled.

The ring also resets after ten seconds without a valid sample or if local tick
handling detects a discontinuity that cannot be represented safely.

### Axes and Projection

- The visible X range is always the latest 10 seconds. Before it fills, the
  trace grows from left to right; afterward it scrolls continuously.
- X labels are fixed at 0.00s, 2.50s, 5.00s, 7.50s, and 10.00s.
- Y range is visible minimum/maximum plus 10% padding.
- A flat signal gets a nonzero span based on its magnitude and display
  resolution, so the line remains centered and labels do not collapse.
- Four Y labels are generated in engineering notation with the active unit.
  Precision is reduced only as needed to fit the fixed label column; sign and
  unit are never clipped.
- The 500 storage buckets are aggregated into at most 240 evenly spaced render
  columns. Each projected column retains its minimum and maximum; connections
  between columns retain the trend shape. This bounds target line-command work
  while preserving peaks.

## Runtime Architecture

```text
USART1 RX interrupt -> 512-byte byte ring -> main-loop protocol parser
                                              |
                                              +-> ui_model/status_bar
                                              +-> trend_buffer

ui_model -> main_display upper-frame description
trend_buffer -> caller-owned 240-column trend projection
                    |
                    +-> simulator canvas renderer
                    +-> cooperative LT7680 target renderer
```

### UART Prerequisite

The current target polls `USART1 RXNE` from the main loop. Large-glyph and trend
rendering can therefore lose incoming bytes. Before enabling the new scene, RX
must use an interrupt-fed 512-byte ring buffer. The main loop drains available
bytes before rendering. Rendering itself is cooperative: text runs and graph
segments are emitted in bounded slices, then control returns to drain RX before
the next slice. A slice must remain below 10ms in target profiling; the renderer
must not execute an unbounded full-glyph or full-graph loop while input waits.
On queue overflow, increment a diagnostic counter, discard input until the next
`0x0D`, and then resume parser synchronization.

Keypad TX remains independent. RX interrupt handling must not perform protocol
parsing or display work.

### Refresh Policy

- Every valid reading is inserted into the trend buffer, independent of display
  refresh throttling.
- Main reading and metadata repaint at no more than 10Hz.
- Trend grid and curve repaint at no more than 5Hz.
- The rates above are scheduling ceilings, not permission to block: each paint
  is resumed across bounded renderer slices until its dirty region is complete.
- A graph frame uses no more than 240 projected columns. The renderer completes
  an older projection before publishing the newest one; intermediate graph
  frames may be coalesced, but input samples are never discarded.
- Static bars and grid are retained when possible and redrawn after any region
  clear that overlaps them.
- Text and plot areas have independent dirty flags.
- The first implementation redraws the bounded graph region with LT7680 fill
  and geometry primitives. BTE scrolling is deferred unless target profiling
  proves the 5Hz budget cannot be met.

## Component Changes

### Shared Pure Logic

- Add `trend_buffer.c/.h` to `firmware/src` and mirror it byte-for-byte into the
  CubeMX Core tree.
- Extend `status_bar` to the required authentic indicators and ODS bit values.
- Extend `ui_model` with derived function/range/filter metadata only where the
  protocol can support it.
- Replace the current compact `main_display` frame with a complete industrial
  upper-display frame description. Keep it HAL-free and host-testable.
- Add formatting helpers for Read/s, function heading, input impedance, Y-axis
  values, and fixed X-axis labels.
- `trend_buffer` projects into a fixed caller-owned 240-column array. The
  projection and history are static storage, never copied into a render frame
  or allocated on the 1KB reserved target stack.

### Target Hardware Layer

- Add interrupt-backed UART RX queue support to `hal_board`.
- Update `main.c` to drain RX, feed trend samples, track independent dirty
  regions, and advance the cooperative renderer through transformed coordinates.
- Use existing LT7680 fill, line, polyline, and pixel/text primitives. Add only
  the smallest missing transformed drawing helpers.
- Make the text-run renderer consume the existing dedicated micro, ohm, degree,
  and plus/minus glyphs rather than treating their UTF-8 bytes as `?`.

### Simulator

- Retarget the canvas scene to the same final layout and semantics.
- Keep the 960x320 panel and 320x960 framebuffer previews.
- Provide a complete design preset with `+03.68900 V`, 10V AUTO, MOVING 10,
  GPIB 16, active annunciators, and a deterministic RC/sawtooth trace.
- Make explicit that simulator-only fields are scenario inputs, not claims that
  the panel protocol supplies them.
- Preserve useful scene controls and layout verification, but remove controls
  that only support the obsolete reading-page geometry.

## Failure and Empty States

- No samples: keep the grid visible and show `WAITING FOR DATA` in muted grey.
- Special readings: retain existing red `OVERFLOW` and grey `----` behavior;
  neither enters the trend.
- UART overflow: count it, resynchronize on `0x0D`, and keep the last valid UI.
- LT7680 command failure: record diagnostics and retry on the next scheduled
  refresh; protocol reception continues.
- Unknown range, filter detail, address, or impedance: use the explicit reduced
  forms defined above. Never substitute the simulator preset on hardware.

## Verification

### Host Tests

- Literal ODS status mappings, including REM/TALK/LSTN/SRQ, TRIG, MATH, FILT,
  REL, AUTO, ERR, and BUFFER.
- FAST/MED/SLOW to 500/50/5 Read/s formatting.
- Reading preservation, function inference, and impedance rules.
- Prefix normalization and physical-dimension changes.
- 500-bucket wrap, min/max peak preservation, missing buckets, timeout reset,
  and tick discontinuity handling.
- 500-to-240-column projection, including min/max preservation across each
  aggregation interval and the maximum line-command budget.
- Automatic Y range, flat-signal range, engineering labels, and fixed 10-second
  X labels.
- UART queue order, overflow count, and `0x0D` resynchronization.
- Cooperative renderer slicing: bounded work, resumable region state, and RX
  draining between slices.
- No trend insertion for special or malformed readings.

### Automated Acceptance

Run all of the following:

```sh
firmware/tests/run_tests.sh
make -C firmware
node sim/verify.js
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
arm-none-eabi-size KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf
```

Confirm shared pure-logic files are byte-identical between `firmware/src` and
`KEITHLEY_2000_LCD/Core/{Src,Inc}`. The linked image must remain within 64KB
Flash and 20KB RAM with the existing heap and stack reservations. No trend or
projection array may be automatic stack storage.

### Hardware Acceptance

- Inject each ODS status bit independently and verify the TFT label against the
  physical Keithley annunciator.
- Stream valid reading messages while repeatedly repainting large digits and
  the graph; verify no parser corruption or UART overflow at the observed link
  rate. Record the worst renderer slice duration and require it to remain below
  10ms.
- Confirm reading refresh reaches 10Hz and trend refresh reaches 5Hz without
  delaying keypad passthrough.
- Confirm a 500 Read/s synthetic source preserves short min/max excursions in
  the 50Hz trend buckets.
- Validate graph direction, axes, clipping, text fit, color, and 960x320 to
  320x960 transpose on the physical panel.

## Non-Goals

- Querying Keithley SCPI for range, filter count/type, or GPIB address.
- Adding a settings screen or making simulator metadata editable on hardware.
- Implementing DMM6500 buffers, scripts, digitize mode, or DCCPL.
- BTE graph scrolling before target profiling demonstrates a need.
- Changing keypad scene navigation or adding new full-screen scenes.

## References

- `K2000_Panel_Protocol.ods` and
  `docs/2026-08-14-sim-content-vs-ods-audit.md`
- Keithley Model 2000 User's Manual, sections 2-3 and 4
- Keithley Model 2000 specifications: DCV input resistance and operating rates
- `docs/adr/0001-view-space-vs-panel-transform.md`
- `docs/adr/0002-independent-scenes.md`
