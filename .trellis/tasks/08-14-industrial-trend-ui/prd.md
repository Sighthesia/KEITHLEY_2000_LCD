# Implement Industrial Trend UI

## Goal

Replace the current reading-page presentation with a dark industrial Keithley
2000 measurement screen that keeps the live reading prominent while adding a
10-second real-time trend view. The same observable behavior must be available
in the browser simulator, offline firmware skeleton, and CubeMX target.

## User Value

An operator can identify the active Keithley measurement, host/interface state,
rate, and recent voltage behavior from one dense screen without confusing
Keithley 2000 state with DMM6500 terminology or simulator-only metadata.

## Requirements

### Screen and Terminology

- Use the existing 960x320 logical landscape space and current transpose path.
- Use a black industrial theme with neon green valid readings and trend data,
  cyan graph/axis labels, white labels, and muted unavailable state.
- Use authentic labels `REM`, `TALK`, `LSTN`, `SRQ`, `TRIG`, `FILT`, `REL`,
  `MATH`, and `AUTO`; do not use `DIGITIZE`, `defbuffer1`, `No Script`, or
  `DCCPL`.
- Render the following fixed-height regions: 48px status/function header,
  96px reading band, 24px parameter band, 24px graph header, and 128px trend
  region. The total must remain exactly 320px.
- Preserve host-provided numeric text and trailing zeros. Do not synthesize
  precision.
- Infer a function heading where possible, including `DC VOLTAGE`,
  `AC VOLTAGE`, `DC CURRENT`, `AC CURRENT`, `2W OHM`, `4W OHM`, `FREQUENCY`,
  `PERIOD`, and `TEMPERATURE`; otherwise use `MEASUREMENT`.
- Derive input impedance only when function and range are known; otherwise show
  `Zin: --`. For known Keithley 2000 DC/AC ranges use the documented impedance
  and the existing `Ω` glyph.

### Status and Unknown Values

- Use literal ODS status bits: `0x06`: REM `0x08`, TALK `0x04`, LSTN `0x02`,
  SRQ `0x01`; `0x08`: HOLD `0x10`, TRIG `0x08`, FAST `0x04`, MED `0x02`,
  SLOW `0x01`; `0x09`: REL `0x40`, FILT `0x20`, AUTO `0x10`, ERR `0x08`,
  BUFFER `0x02`; `0x07` MATH is `0x20`.
- Display rate as `500 Read/s`, `50 Read/s`, or `5 Read/s` for FAST, MED, or
  SLOW. Do not show NPLC in this screen.
- Use `CONT` only as a clearly local indication that TFT trend capture is
  running, not as an original Keithley annunciator.
- When target protocol data does not provide range, filter detail, or GPIB
  address, show reduced forms (`Range: AUTO/MANUAL`, `Filter: ON/OFF`,
  `GPIB: --`). The simulator may provide a complete design preset with 10V,
  MOVING 10, and address 16.
- Show `BUFFER: RECALL` when the BUFFER bit is set, otherwise
  `BUFFER: IDLE (1024 MAX)`; 1024 is capacity, not live count.

### Trend Behavior

- Store the latest 10 seconds in 500 20ms buckets at 50Hz.
- Preserve each bucket's minimum and maximum so short excursions survive input
  rates up to 500 readings/s.
- Project history into no more than 240 graph columns.
- Use fixed X labels `0.00s`, `2.50s`, `5.00s`, `7.50s`, `10.00s`.
- Automatically scale Y to visible samples with 10% padding; flat signals must
  receive a nonzero display span.
- Exclude `OVERFLOW`, `----`, startup placeholders, and malformed values.
- Clear history when the physical measurement dimension changes or after ten
  seconds without a valid sample.

### Runtime Reliability

- Replace target main-loop-only UART polling with a 512-byte interrupt-fed RX
  ring. Parsing and drawing must remain outside the interrupt handler.
- On RX overflow, count the error, discard until the next `0x0D`, and restore
  parser synchronization.
- Keep rendering cooperative: each drawing slice must be below 10ms in target
  profiling and return to RX draining between slices.
- Limit reading repaint to 10Hz and trend repaint to 5Hz without dropping input
  samples or delaying keypad passthrough.

### Synchronization and Documentation

- Keep pure logic source files byte-identical between `firmware/src` and
  `KEITHLEY_2000_LCD/Core/{Src,Inc}`.
- Update `sim/index.html`, `sim/README.md`, and `sim/verify.js` for the new
  scene and simulator-only preset semantics.
- Update ADR-0002 because the approved product behavior makes the trend view
  resident in the reading scene rather than an independent full-screen scene.

## Acceptance Criteria

- [ ] Simulator renders the approved 960x320 five-region layout with a valid
      `+03.68900 V` design preset, industrial colors, authentic labels, and a
      deterministic RC/sawtooth trace.
- [ ] Host tests cover ODS status bits, rate labels, function/impedance rules,
      special-value exclusion, trend bucket wrap, min/max preservation,
      500-to-240 projection, automatic axes, and timeout reset.
- [ ] Target UART receives through the interrupt ring, handles overflow and
      `0x0D` resynchronization, and does not parse or draw inside the ISR.
- [ ] Target renderer keeps each cooperative slice below 10ms and maintains
      the specified reading/trend refresh ceilings.
- [ ] Offline and CubeMX builds pass with no warnings and remain within 64KB
      Flash / 20KB RAM reservations; trend arrays are static, not stack-local.
- [ ] Pure shared logic files are byte-identical across the two firmware trees.
- [ ] `node sim/verify.js` passes and its no-DOM path remains safe.
- [ ] Hardware acceptance verifies each migrated ODS bit against physical
      annunciators, graph direction, text fit, color, and transpose behavior.

## Out Of Scope

- SCPI queries for range, filter count/type, or GPIB address.
- A settings screen or target-side configuration UI.
- DMM6500 buffers, scripts, digitize mode, or DCCPL.
- BTE scrolling before profiling proves bounded redraw cannot meet 5Hz.
- New scene navigation or changing keypad passthrough semantics.

## Blocking Open Questions

None. Hardware status-bit validation remains an acceptance experiment; it does
not block implementation of the selected ODS mapping.

## Evidence

- Approved design: `docs/superpowers/specs/2026-08-14-k2000-industrial-trend-ui-design.md`
- Protocol audit: `docs/2026-08-14-sim-content-vs-ods-audit.md`
- Current source boundary: `AGENTS.md`, `docs/adr/0001-view-space-vs-panel-transform.md`
- Keithley terminology/specification: cited in the approved design document.
