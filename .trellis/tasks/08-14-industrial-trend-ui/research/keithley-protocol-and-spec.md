# Research: Keithley 2000 Protocol and Display Constraints

## Local Evidence

- `docs/2026-08-14-sim-content-vs-ods-audit.md` records observed RX status
  groups and the conflict between literal ODS columns and the older bit7-first
  implementation convention.
- `firmware/src/status_bar.c` currently exposes only HOLD/REM/REL/TRIG/AUTO/ERR.
- `firmware/src/main.c` currently uses a main-loop USART1 RXNE poll and has no
  receive queue or interrupt parser boundary.
- `docs/adr/0001-view-space-vs-panel-transform.md` requires all UI coordinates
  to remain in logical 960x320 space.
- `docs/adr/0002-independent-scenes.md` describes the earlier independent
  trend-scene decision; the approved resident trend UI supersedes that display
  placement while retaining scene-independent trend data.

## External Reference

The Keithley Model 2000 User's Manual and Tektronix/Keithley specifications
confirm these product terms and constraints:

- Original annunciators include REM, TALK, LSTN, SRQ, REL, FILT, BUFFER, MATH,
  HOLD, TRIG, FAST, MED, SLOW, AUTO, and ERR.
- Factory GPIB primary address is 16, but address is configurable and is not
  supplied by the observed panel display protocol.
- FAST/MED/SLOW correspond to 0.1/1/10 PLC operating modes. This UI displays
  the project-approved nominal rates 500/50/5 Read/s instead of NPLC.
- DC voltage input resistance is greater than 10 GOhm on 100mV, 1V, and 10V
  ranges; 100V and 1000V ranges are 10 MOhm class. The target must not display
  a fixed 10 MOhm value for an unknown 10V DC range.

## Planning Consequence

The simulator may use a complete 10V/GPIB-16 design preset for visual review,
but target rendering must expose unknown range/address/filter details rather
than claiming values not present in the panel protocol.
