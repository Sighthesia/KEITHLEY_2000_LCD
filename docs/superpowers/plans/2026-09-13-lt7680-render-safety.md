# LT7680 Render Safety Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden the LT7680 Flash DMA, BTE blit, and RIF cache paths against invalid geometry and leaked Canvas state without changing the unresolved RIF resource-format decision.

**Architecture:** Keep the existing custom RIF RGB565/DMA/BTE path unchanged at the behavior level. Add pure range-validation helpers where possible, use them from the hardware APIs, and convert cache construction to one cleanup path that restores the saved Canvas base and width before returning.

**Tech Stack:** C, STM32 HAL/CMake, host GCC tests, shell test runner, LT7680 SPI register driver.

## Global Constraints

- Do not accept `80x44` as a valid RIF format until a real Flash entry and payload are independently verified.
- Do not switch to the LT7680 standard external-font API in this change.
- Shared logic must be changed in `firmware/src/` first, then synchronized manually to the CubeMX tree.
- The host UART remains silent by default: `K2000_UART_LOG=0`, `K2000_PERF_LOG=0`.
- Generated `.img` files and unrelated worktree changes must not be committed.

---

### Task 1: Add pure transfer-range validation

**Files:**
- Create: `firmware/src/lt7680_transfer_range.h`
- Create: `firmware/src/lt7680_transfer_range.c`
- Test: `firmware/tests/test_lt7680_transfer_range.c`
- Modify: `firmware/tests/run_tests.sh`

**Interfaces:**
- Produces `bool lt7680_validate_2d_source(uint32_t address, uint16_t stride_pixels, uint16_t width_pixels, uint16_t height, uint32_t sdram_limit)`.
- Produces `bool lt7680_validate_2d_destination(uint32_t base, uint16_t stride_pixels, uint16_t x, uint16_t y, uint16_t width_pixels, uint16_t height, uint32_t sdram_limit)`.

- [ ] **Step 1: Write failing boundary tests**

```c
assert(lt7680_validate_2d_source(0x00300000u, 128u, 80u, 44u,
                                 0x01000000u));
assert(!lt7680_validate_2d_source(0x00FFF000u, 320u, 128u, 68u,
                                  0x01000000u));
assert(!lt7680_validate_2d_source(0x00300000u, 79u, 80u, 44u,
                                  0x01000000u));
assert(lt7680_validate_2d_destination(0x00100000u, 320u, 0u, 0u,
                                      128u, 68u, 0x01000000u));
assert(!lt7680_validate_2d_destination(0x00100000u, 320u, 0u, 892u,
                                       128u, 68u, 0x01000000u));
```

- [ ] **Step 2: Add the new source to `firmware/tests/run_tests.sh` and run the focused test**

Run: `cd firmware && ./tests/run_tests.sh`

Expected: the new assertions fail before the implementation exists.

- [ ] **Step 3: Implement overflow-safe last-byte calculations**

Use 64-bit intermediates. For a source, calculate `address + ((height - 1) * stride_pixels + width_pixels) * 2`. For a destination, calculate `base + (y + height - 1) * stride_pixels * 2 + (x + width_pixels) * 2`; reject zero dimensions, `stride_pixels < width_pixels`, arithmetic wrap, and an end greater than `sdram_limit`.

- [ ] **Step 4: Run the focused tests again**

Run: `cd firmware && ./tests/run_tests.sh`

Expected: PASS, including the last-row padding and overflow cases.

- [ ] **Step 5: Commit the pure validation unit**

```sh
git add firmware/src/lt7680_transfer_range.* firmware/tests/test_lt7680_transfer_range.c firmware/tests/run_tests.sh
git commit -m "test: add lt7680 transfer range validation"
```

### Task 2: Apply validation to LT7680 DMA and BTE APIs

**Files:**
- Modify: `firmware/src/lt7680_gfx.c`
- Modify: `firmware/src/lt7680_gfx.h`
- Modify: `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c`
- Modify: `KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h`

**Interfaces:**
- Consumes the pure range validators from Task 1 or an equivalent local implementation if the hardware tree does not link the host-only unit.
- Preserves existing public signatures for `lt7680_flash_dma_tile_to_canvas()` and `lt7680_gfx_blit()`.

- [ ] **Step 1: Add failing integration assertions or host-equivalent checks**

Cover these exact invalid calls:

```c
lt7680_flash_dma_tile_to_canvas(0u, 0x00100000u, 320u,
                                0u, 892u, 128u, 68u);
lt7680_gfx_blit(1u, 0x00FFF000u, 320u,
                0u, 0u, 128u, 68u);
lt7680_gfx_blit(1u, 0x00300000u, 79u,
                0u, 0u, 80u, 44u);
```

Each call must return `LT7680_ERR_PARAM` before writing a start register.

- [ ] **Step 2: Apply complete destination validation to Flash DMA tile**

Replace the current packed-width end calculation with the full-stride calculation, while retaining the existing panel/page and Flash address checks.

- [ ] **Step 3: Apply source validation to BTE blit**

Reject `src_stride < w` and validate the final source row and pixel against `0x01000000u` before programming `BTE_CTRL0`.

- [ ] **Step 4: Keep target checks unchanged and preserve error precedence**

Parameter errors must be returned before SPI traffic. Existing `LT7680_OK` and bus/timeout behavior must remain unchanged for valid operations.

- [ ] **Step 5: Run host tests and Release compile**

Run: `cd firmware && ./tests/run_tests.sh`

Run: `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`

Expected: both pass.

- [ ] **Step 6: Commit API validation**

```sh
git add firmware/src/lt7680_gfx.* KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h
git commit -m "fix(display): validate lt7680 dma and bte ranges"
```

### Task 3: Make RIF cache construction restore Canvas state on every exit

**Files:**
- Modify: `firmware/src/rif_tile_cache.c`
- Modify: `KEITHLEY_2000_LCD/Core/Src/rif_tile_cache.c`
- Test: `firmware/tests/test_rif_tile_cache.c` or the existing cache test location

**Interfaces:**
- Preserves `rif_tile_cache_prepare()` signature and return values.
- Produces the invariant: after any failure following the initial snapshot, the function attempts to restore the saved Canvas base and width and leaves `entry->ready == 0`.

- [ ] **Step 1: Add a failure-path test using mocked Canvas operations**

Inject failure at Flash read, Canvas base write, Canvas width write, pixel write, and CRC/validation completion. Assert that the mock restore calls receive the saved `cvssa` and `canvas_stride`.

- [ ] **Step 2: Run the focused cache test and confirm failure**

Run: `cd firmware && ./tests/run_tests.sh`

Expected: failure-path assertions fail against the current early returns.

- [ ] **Step 3: Convert early returns to one cleanup path**

Track the primary `lt7680_status_t st`, mark the entry unready, jump to cleanup after any post-snapshot error, attempt both Canvas restores, and return the primary error unless restoration fails first or is the only failure.

- [ ] **Step 4: Preserve successful cache metadata behavior**

Only write the ready entry and advance `s_next_address` after all payload writes, CRC, and Canvas restoration succeed.

- [ ] **Step 5: Run tests and Release compile**

Run: `cd firmware && ./tests/run_tests.sh`

Run: `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`

Expected: PASS.

- [ ] **Step 6: Commit cache cleanup**

```sh
git add firmware/src/rif_tile_cache.c KEITHLEY_2000_LCD/Core/Src/rif_tile_cache.c firmware/tests
git commit -m "fix(display): restore canvas after rif cache failure"
```

### Task 4: Synchronize, verify, and record remaining uncertainty

**Files:**
- Modify: `docs/2026-09-13-display-rendering-status.md`
- Modify: `docs/2026-09-13-lt7680-pdf-usage-investigation.md`
- Modify: `docs/2026-09-13-lt7680-open-source-research.md`

- [ ] **Step 1: Compare shared source files**

Run: `diff -q firmware/src/lt7680_gfx.c KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c`

Run: `diff -q firmware/src/rif_tile_cache.c KEITHLEY_2000_LCD/Core/Src/rif_tile_cache.c`

Expected: identical shared logic, aside from documented hardware adapter differences if present.

- [ ] **Step 2: Run the project verification set**

Run: `cd firmware && ./tests/run_tests.sh`

Run: `node sim/verify.js`

Run: `tools/tests/run_tests.sh`

Run: `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`

- [ ] **Step 3: Update docs with tested versus untested claims**

Record that range checks and cache failure restoration are implemented and tested. Keep these items explicitly unresolved: actual RIF resource type, the LT7680A-R 16 bpp DMA width semantics, BTE completion semantics on this die, MISA latch timing, and long-term SDRAM refresh margin.

- [ ] **Step 4: Check the final diff and commit the documentation**

```sh
git diff --check
git add docs/2026-09-13-display-rendering-status.md docs/2026-09-13-lt7680-pdf-usage-investigation.md docs/2026-09-13-lt7680-open-source-research.md
git commit -m "docs: record lt7680 safety verification status"
```
