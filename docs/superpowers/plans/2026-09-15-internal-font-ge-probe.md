# Internal Font GE Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in, static internal `font_text` glyph probe that renders ASCII `8` through the existing CPU 1bpp-to-GE path without using RIF, external Flash, DMA, SDRAM glyph cache, or BTE.

**Architecture:** Reuse the existing `ui_draw_bitmap_slice()` renderer because it already extracts MSB-first 1bpp rows and merges horizontal foreground runs into GE rectangles. Add a compile-time switch and a fixed probe scene at the application integration point; leave the default RIF path unchanged. Add host tests around the existing font bitmap and run-generation behavior rather than duplicating the renderer.

**Tech Stack:** C, STM32CubeMX/HAL, LT7680 GE rectangle fill, host GCC tests, shell test runner, CMake/Ninja Release build.

## Global Constraints

- `K2000_INTERNAL_FONT_PROBE` defaults to `0`; diagnostic behavior must never be enabled by the default firmware.
- The probe must not modify RIF header, directory, payload, geometry rules, or external Flash contents.
- The probe must not call external Flash, Flash DMA, SDRAM glyph cache, BTE, trend rendering, or dual-page submission.
- Shared logic is authored in `firmware/src/` first, then synchronized into `KEITHLEY_2000_LCD/Core/`.
- `K2000_DEMO_FEED`, `K2000_UART_LOG`, `K2000_PERF_LOG`, and `K2000_RIF_DMA_PROBE` remain disabled by default.
- Generated simulation data and generated resource images are not hand-edited or committed.

---

### Task 1: Define the opt-in probe configuration

**Files:**
- Modify: `firmware/src/main.c: compile-time renderer switch block`
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c: matching compile-time renderer switch block`
- Test: `firmware/tests/test_scene.c` or the existing host test that validates default scene behavior; otherwise verify through the host test runner and preprocessor output

**Interfaces:**
- Produces `K2000_INTERNAL_FONT_PROBE`, an integer-like compile-time macro with default value `0`.
- Later tasks use `#if K2000_INTERNAL_FONT_PROBE` only at the application rendering seam.

- [ ] **Step 1: Locate the existing feature-switch definitions and confirm both tree paths**

Run:

```sh
codegraph query "K2000_RIF_DMA_PROBE K2000_DEMO_FEED K2000_UART_LOG"
```

Expected: the existing `main.c` compile-time switch blocks are identified; do not add a new configuration header for this diagnostic-only switch.

- [ ] **Step 2: Add the disabled-by-default macro in both `main.c` files**

Use the existing project style:

```c
#ifndef K2000_INTERNAL_FONT_PROBE
#define K2000_INTERNAL_FONT_PROBE 0
#endif
```

Keep the macro adjacent to the other diagnostic/rendering switches. Do not change any existing defaults.

- [ ] **Step 3: Run the host tests**

Run:

```sh
cd firmware && ./tests/run_tests.sh
```

Expected: all existing tests pass with the new macro disabled.

- [ ] **Step 4: Commit the configuration seam**

```sh
git add firmware/src KEITHLEY_2000_LCD/Core/Inc firmware/tests
git commit -m "feat(display): add internal font probe switch"
```

---

### Task 2: Add host coverage for the internal bitmap contract

**Files:**
- Modify: `firmware/tests/test_font_text.c`
- Inspect only: `firmware/src/font_text.h`, `firmware/src/font_text.c`
- Inspect only: `KEITHLEY_2000_LCD/Core/Inc/font_text.h`, `KEITHLEY_2000_LCD/Core/Src/font_text.c`

**Interfaces:**
- Consumes `font_text_glyph()` and `font_text_width()/font_text_height()`.
- Produces regression coverage proving ASCII `8` resolves to a 12x24, MSB-first, row-packed bitmap and invalid tokens resolve safely.

- [ ] **Step 1: Add the failing assertions for the probe glyph**

Extend `test_font_text.c` with assertions equivalent to:

```c
font_text_glyph_t glyph;
assert(font_text_glyph("8", 1u, &glyph));
assert(glyph.bitmap != 0);
assert(glyph.bytes == 1u);
assert(font_text_width() == 12u);
assert(font_text_height() == 24u);
assert((glyph.bitmap[0] & 0x80u) == 0u || (glyph.bitmap[0] & 0x80u) != 0u);
```

Replace the tautological final line with exact known row expectations after inspecting the generated `8` bitmap; test at least one row with a leftmost pixel, one row with an interior run, and one empty row.

- [ ] **Step 2: Add invalid-input and boundary assertions**

Cover:

```c
assert(!font_text_glyph(0, 1u, &glyph));
assert(!font_text_glyph("", 0u, &glyph));
assert(font_text_glyph("\x01", 1u, &glyph));
assert(glyph.bitmap != 0); /* fallback glyph */
```

Use the existing test style and avoid testing implementation-private arrays.

- [ ] **Step 3: Run only the font host test**

Run the repository’s existing focused command if available; otherwise run:

```sh
cd firmware && ./tests/run_tests.sh
```

Expected: the new assertions pass against the existing generated font source.

- [ ] **Step 4: Commit the font contract tests**

```sh
git add firmware/tests/test_font_text.c
git commit -m "test(display): cover internal font probe glyph"
```

---

### Task 3: Integrate the fixed single-glyph GE probe

**Files:**
- Modify: `firmware/src/main.c`
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`
- Modify: the matching `main.c` switch block from Task 1 if the probe needs a board-specific coordinate constant
- Test: `firmware/tests/test_scene.c` only if the existing scene seam can exercise the compile-time branch without hardware calls

**Interfaces:**
- Consumes `K2000_INTERNAL_FONT_PROBE`, existing `ui_draw_text()`/`ui_draw_bitmap_slice()`, `font_text`, and `ui_fill_rect()`.
- Produces a static probe call that draws exactly one ASCII `8` and does not invoke `rif_draw_job_next_glyph()`, `lt7680_flash_dma_tile_to_canvas()`, `lt7680_gfx_blit()`, or cache preparation when enabled.

- [ ] **Step 1: Add the probe branch at the narrowest existing rendering seam**

Use the existing text renderer instead of creating a second bitmap parser. The enabled branch should be equivalent to:

```c
#if K2000_INTERNAL_FONT_PROBE
    static bool internal_font_probe_drawn;

    if (!internal_font_probe_drawn) {
        if (!ui_draw_text(PROBE_X, PROBE_Y, "8", MAIN_DISPLAY_COLOR_GREEN)) {
            return false;
        }
        internal_font_probe_drawn = true;
    }
    return true;
#else
    /* existing RIF/dynamic scene path */
#endif
```

Use the existing scene/render scheduler lifecycle rather than introducing a new infinite loop. Choose a fixed coordinate inside the active UI bounds and keep the probe on the existing single-page path. Do not call the probe from the default build.

- [ ] **Step 2: Ensure the enabled branch bypasses external resources**

With `K2000_INTERNAL_FONT_PROBE=1`, the static path must not reach:

```c
rif_find_next_tile
rif_tile_cache_prepare
lt7680_flash_dma_tile_to_canvas
lt7680_gfx_blit
```

Keep the normal initialization and GE Canvas setup intact so the experiment tests the display path rather than bypassing it.

- [ ] **Step 3: Add a host compile/test seam if needed**

If `main.c` cannot be tested directly, add only a small pure helper around the existing bitmap-run extraction, with a test-visible signature such as:

```c
uint8_t internal_font_next_run(const uint8_t *row,
                               uint8_t width,
                               uint8_t start,
                               uint8_t *run_x,
                               uint8_t *run_width);
```

The helper must return zero when no foreground run remains and must use `0x80u >> bit` MSB-first extraction. Do not add a second production renderer if `ui_draw_bitmap_slice()` can be reused directly.

- [ ] **Step 4: Verify both compile-time modes**

Run the host test suite with the default macro and compile a probe-mode target using the existing project build command with the macro override:

```sh
cd firmware && make clean
cd firmware && make CFLAGS='-DK2000_INTERNAL_FONT_PROBE=1'
```

If the existing `firmware/make` target reaches its known `syscalls` link failure, record the failure after confirming all changed C files compile. Do not change unrelated syscalls infrastructure.

- [ ] **Step 5: Commit the probe integration**

```sh
git add firmware/src/main.c KEITHLEY_2000_LCD/Core/Src/main.c firmware/tests
git commit -m "feat(display): add internal font ge probe"
```

---

### Task 4: Synchronize and run complete verification

**Files:**
- Modify: `docs/2026-09-15-internal-font-ge-probe-design.md` only if implementation details require a factual clarification
- Add: `.superpowers/sdd/2026-09-15-internal-font-ge-probe/task-4-report.md` as local task report; this ignored SDD artifact is not a product source file

**Interfaces:**
- Consumes the completed configuration, tests, and probe branch.
- Produces verified source-tree synchronization and explicit default/probe-mode build results.

- [ ] **Step 1: Compare the shared changes**

Run:

```sh
diff -q firmware/src/font_text.c KEITHLEY_2000_LCD/Core/Src/font_text.c
diff -q firmware/src/font_text.h KEITHLEY_2000_LCD/Core/Inc/font_text.h
git diff --check
```

If `main.c` has pre-existing CubeMX hardware differences, compare only the new probe block and document the boundary; do not overwrite unrelated user changes.

- [ ] **Step 2: Run the full software verification**

Run exactly:

```sh
cd firmware && ./tests/run_tests.sh
node sim/verify.js
tools/tests/run_tests.sh
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

Expected: host tests, simulator, tools tests, and the existing Release build target pass. Report `ninja: no work to do` as an up-to-date build, not as a fresh rebuild.

- [ ] **Step 3: Confirm default behavior remains external-resource based**

Inspect the default preprocessor value and changed call sites. Confirm `K2000_INTERNAL_FONT_PROBE=0` leaves the RIF path active and does not add generated BIN/RIF files.

- [ ] **Step 4: Commit documentation/report changes if needed**

```sh
git add docs/2026-09-15-internal-font-ge-probe-design.md
git commit -m "docs: record internal font probe verification"
```

Do not stage `docs/IMG_*.jpg` deletions, `.codegraph/`, `.embeddedskills/`, generated `.img` files, or unrelated worktree changes.

## Hardware Handoff

After software verification, flash only the explicitly built Release ELF with `K2000_INTERNAL_FONT_PROBE=1` for the diagnostic experiment. Record:

1. GE full-screen color fill result.
2. Static internal `8` result.
3. Whether any black block or speckle remains.
4. Whether the artifact is fixed-position or glyph-dependent.
5. `MISA`, `CVSSA`, `CVS_IMWTH`, `STATUS` before and after the probe.

Restore `K2000_INTERNAL_FONT_PROBE=0` and rebuild before any normal product flash.
