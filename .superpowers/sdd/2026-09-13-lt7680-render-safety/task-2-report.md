# Task 2 Report

## Result

DONE

## Changes

- Applied full-stride destination validation to `lt7680_flash_dma_tile_to_canvas()` in both source trees. The destination end is calculated from the final row (`y + height - 1`) and the exclusive final pixel (`x + width`), using 64-bit intermediates.
- Applied BTE source validation to `lt7680_gfx_blit()` in both source trees. Calls now reject `src_stride < w` and source rectangles whose final row/pixel exceeds `0x01000000u` before `BTE_CTRL0` is programmed.
- Preserved existing Flash address, panel/page, destination coordinate, `LT7680_OK`, bus, and timeout behavior for valid operations.
- Added the two APIs and equivalent local range checks to `firmware/src`, because the host-only Task 1 validator translation unit is not linked by the firmware Makefile.
- Did not modify RIF format rules or unrelated worktree changes.

## Verification

- `test_lt7680_transfer_guard.c` covers the two exact BTE-invalid calls from the Task 2 brief and the corresponding real Flash-DMA destination overflow boundary after `lt7680_gfx_init()` with a 320x960, 16bpp panel. Each call asserts `LT7680_ERR_PARAM` and zero `DMA_CTRL`/`BTE_CTRL0` writes, covering the pre-start-register guard requirement.
- The Flash-DMA call printed in the Task 2 brief uses `canvas_base=0x00100000`. With the API's full-stride destination formula, that range ends below `0x01000000` even at `y=892`, so it is valid rather than an invalid boundary. The test uses `canvas_base=0x00F80000`, which is a real invalid destination boundary for the same geometry.
- `cd firmware && ./tests/run_tests.sh`: PASS. All host tests passed, including `test_lt7680_transfer_guard` and `test_lt7680_transfer_range`. An initial run correctly exposed that the brief's Flash-DMA call returned `LT7680_OK`; the final test uses the real overflow boundary documented above.
- `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`: PASS. RAM 16080 B / 20 KB (78.52%); Flash 34616 B / 64 KB (52.82%).
- `cd firmware && make`: source compilation PASS; final link BLOCKED by the existing `syscalls` setup, which lacks `_fstat`, `_isatty`, `_kill`, and `_getpid` symbols. No Task 2 source error was reported.
- `git diff --check`: PASS.

## Scope Checks

The three brief boundary cases are rejected during the parameter guard, before any SPI register write or BTE/DMA start register write:

- Flash DMA: `0x00F80000`, stride `320`, `y=892`, `128x68` (a real full-stride destination overflow boundary; the Task 2 brief's `0x00100000` is valid under the current API semantics).
- BTE source address: `0x00FFF000`, stride `320`, `128x68`.
- BTE source stride: address `0x00300000`, stride `79`, `80x44`.

## Questions

None for Task 2. The firmware Makefile link failure is pre-existing infrastructure debt and should be handled separately if a complete firmware ELF is required.
