# Technical Design

## Boundaries

The implementation has four explicit layers:

1. `k2000_proto` remains the byte-to-event boundary. It owns protocol
   synchronization and emits status/field events; it does not format pixels.
2. `ui_model`, `status_bar`, `reading_split`, and the new `trend_buffer` own
   host-derived state and pure transformations. These files are the shared
   source copied byte-for-byte into the CubeMX tree.
3. `main_display` owns layout and display-ready frame data. It exposes upper
   metadata, reading text, axis labels, and a caller-owned trend projection,
   without depending on HAL or LT7680.
4. `sim/index.html` and CubeMX `main.c` are rendering adapters. The simulator
   draws its canvas; the target uses transformed LT7680 primitives and a
   cooperative renderer. Neither adapter reimplements protocol semantics.

## Data Flow

```text
USART1 RX ISR -> 512-byte ring -> main loop -> k2000_proto
                                      |              |
                                      |              +-> ui_model/status_bar
                                      |              +-> trend_buffer sample
                                      |
                                      +-> cooperative render scheduler
                                           -> main_display frame/projection
                                                -> simulator or LT7680
```

The exact host reading string remains in `ui_model`. Trend samples use a
bounded locale-independent decimal parser and normalized float values. A
dimension token is kept with the trend state so changing from voltage to
resistance clears history. The parser never allocates or calls formatted I/O.

## Trend Contract

`trend_buffer` exposes initialization, reset, sample insertion, timeout update,
window statistics, and projection into a caller-owned array of at most 240
columns. Each history bucket contains occupancy, min, and max. Projection uses
the current time window and preserves both extrema for each aggregate column.
All arrays are static or caller-owned; no large automatic objects are allowed
on the target stack.

The frame formatter consumes trend statistics and emits four Y labels, five X
labels, plot bounds, and normalized line points. It must be deterministic for a
given model, history, and tick value, so simulator verification can exercise it
without a display.

## Status Migration

Replace the existing bit7-first indicator table rather than adding a second
compatibility interpretation. Add unit tests for every new label and keep the
mapping in one status table. The hardware acceptance test is the final source
of truth if the historical ODS record disagrees with the physical board.

## UART and Scheduling

Enable USART1 RX interrupt and a fixed 512-byte ring in the hardware layer.
The ISR only reads the data register, writes the ring, and records overflow.
The main loop drains a bounded batch, feeds the parser, samples trends, then
advances rendering. If overflow recovery is active, bytes are ignored until
`0x0D` before normal parser input resumes.

Rendering uses resumable state for upper text, graph background, axes, and curve
segments. Each call has a bounded operation budget and returns before 10ms. A
new dirty frame may supersede an unfinished graph projection, but samples and
the latest reading are never discarded.

## Rendering Compatibility

All UI coordinates remain logical 960x320 and pass through the existing panel
transform. The target first uses tested rectangle/line/text primitives; BTE is
explicitly deferred. Text symbols use the existing dedicated glyph API where
UTF-8 protocol symbols cannot be passed directly through the ASCII renderer.

The simulator's design preset is allowed to supply fields the panel protocol
does not expose. The target must render reduced unknown forms instead. Both
paths share constants and formatting rules where practical, with `verify.js`
asserting layout dimensions, axis labels, and output smoke characteristics.

## Migration and Rollback

Implement pure logic and tests before replacing the target renderer. Keep the
verified color-bar/self-test path intact. If the new renderer fails on target,
disable the scene dirty flag and restore the prior reading renderer while
retaining protocol and trend unit tests; do not alter reset, panel init, or
verified LT7680 register setup as part of this feature.

Update ADR-0002 in the same documentation change to record that this approved
screen is a deliberate exception to the earlier independent-trend-scene plan:
the trend data remains scene-independent, but the first display surface is
resident in scene 0.
