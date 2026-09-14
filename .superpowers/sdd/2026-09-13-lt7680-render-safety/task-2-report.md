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

- `cd firmware && ./tests/run_tests.sh`: PASS. All host tests passed, including `test_lt7680_transfer_range`.
- `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`: PASS. RAM 16080 B / 20 KB (78.52%); Flash 34616 B / 64 KB (52.82%).
- `cd firmware && make`: source compilation PASS; final link BLOCKED by the existing `syscalls` setup, which lacks `_fstat`, `_isatty`, `_kill`, and `_getpid` symbols. No Task 2 source error was reported.
- `git diff --check`: PASS.

## Scope Checks

The three brief boundary cases are rejected during the parameter guard, before any SPI register write or BTE/DMA start register write:

- Flash DMA: `0x00100000`, stride `320`, `y=892`, `128x68`.
- BTE source address: `0x00FFF000`, stride `320`, `128x68`.
- BTE source stride: address `0x00300000`, stride `79`, `80x44`.

## Questions

None for Task 2. The firmware Makefile link failure is pre-existing infrastructure debt and should be handled separately if a complete firmware ELF is required.
