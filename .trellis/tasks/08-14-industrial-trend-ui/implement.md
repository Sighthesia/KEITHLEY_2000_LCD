# Implementation Plan

## Ordered Checklist

1. **Baseline and source synchronization**
   - Record current test/build results and dirty paths.
   - Locate every shared source/header mirror and preserve unrelated
     `.opencode/package.json` changes.
   - Add focused tests before changing behavior where practical.

2. **Protocol/status model**
   - Replace the status indicator table with the approved literal ODS bits.
   - Add TALK/LSTN/SRQ/MATH/FILT/BUFFER coverage and rate formatting.
   - Extend model fields only for values supported by observed protocol data.
   - Mirror and diff all shared changes into the CubeMX tree.

3. **Trend buffer and pure formatting**
   - Add static 500-bucket trend storage and bounded numeric normalization.
   - Implement dimension reset, timeout reset, min/max preservation, ring wrap,
     and 240-column projection.
   - Replace the compact frame contract with the approved five-region layout,
     function/impedance/rate formatting, and automatic axes.
   - Add host tests for every acceptance rule.

4. **UART receive reliability**
   - Add the 512-byte USART1 RX interrupt ring and overflow/resynchronization
     diagnostics to the hardware layer.
   - Keep ISR work limited to byte capture and counters.
   - Change the main loop to drain RX before advancing parser/model/render work.

5. **CubeMX renderer**
   - Add cooperative render state and transformed drawing helpers.
   - Render status/function/reading/parameters/graph header and grid/curve using
     existing tested LT7680 primitives.
   - Use dedicated symbol glyphs and avoid unbounded draw loops.
   - Preserve reset, panel init, color-bar self-test, and keypad passthrough.

6. **Simulator and documentation**
   - Replace simulator scene geometry and add the complete design preset plus
     deterministic trace.
   - Update no-DOM verification, README controls, and layout assertions.
   - Update ADR-0002 to reflect resident trend rendering in scene 0.

7. **Cross-layer synchronization and validation**
   - Copy shared pure logic files and run byte-for-byte diffs.
   - Run host tests, offline make, simulator verification, and authoritative
     CubeMX Release build/size commands.
   - Inspect generated artifacts and ensure no large trend object is automatic
     stack storage.

8. **Hardware readiness review**
   - Run the full-scope quality check against all affected layers.
   - Prepare status-bit injection, sustained UART, 500 Read/s peak-preservation,
     10ms slice, refresh-rate, and physical transpose checks.
   - Do not flash hardware until the build uses the authoritative
     `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf` path and the
     verified reset/self-test sequence remains intact.

## Validation Commands

```sh
firmware/tests/run_tests.sh
make -C firmware
node sim/verify.js
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
arm-none-eabi-size KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf
git diff --check
```

## Review Gates

- Pure logic tests must pass before target or simulator renderer replacement.
- Shared files must be synchronized and diff-clean before each build.
- Simulator verification must pass before target hardware review.
- Final quality check must inspect protocol, pure logic, simulator, hardware,
  docs, memory, and stack behavior together.
- Any target status-bit discrepancy returns to the single status table; no dual
  mapping or silent compatibility branch is allowed.

## Rollback Points

- Before status migration: revert only status/model changes if protocol tests
  expose an incorrect assumption.
- Before renderer replacement: retain the previous target renderer behind a
  compile-time fallback for bring-up.
- Before hardware flashing: require clean authoritative ELF build, size check,
  and preserved color-bar/self-test path.
