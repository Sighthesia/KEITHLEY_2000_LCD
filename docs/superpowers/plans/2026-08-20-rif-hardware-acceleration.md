# RIF Hardware Acceleration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move external RIF large-glyph rendering from per-color-run GE operations to LT7680 Flash-to-SDRAM transfer and one BTE blit per cached glyph.

**Architecture:** Add a read-only SFCS1 DMA primitive that transfers a bounded RIF payload into SDRAM. Validate it with one known glyph while display output is disabled, then build a framebuffer-oriented glyph cache outside canvas pages `0x000000` and `0x100000`. Once a cache entry is verified, `ui_draw_external_digits()` uses `lt7680_gfx_blit()` and falls back to the existing renderer on any failure.

**Tech Stack:** STM32F103C8T6, LT7680A-R SPI host interface, LT7680 SPI Master/DMA, external W25Q64JV U5, RGB565 SDRAM, C11, CMake/Ninja, host GCC tests.

## Global Constraints

- U5 responds through LT7680 `SFCS1`, not `SFCS0`.
- JEDEC read is stable as `00 EF 40 17`; parsed ID is `EF4017`.
- U5 is read-only for this work. No write-enable, program, erase, QE, or status-register modification is permitted.
- Do not enable Quad/Dual Flash commands or hardware font-ROM mode.
- Keep display output disabled during the first DMA/staging probe.
- Never place staging or cache data over SDRAM canvas page 0 or page 1.
- The framebuffer orientation remains `fb_x = ui_y; fb_y = ui_x`.
- The MCU must not allocate a complete 16 KiB input tile and a second complete 16 KiB output tile simultaneously.
- Preserve the existing GE/Flash renderer as a fallback until hardware acceleration passes staged acceptance.
- Release target must remain below 64 KiB Flash and 20 KiB RAM.

---

### Task 1: Add Bounded SFCS1 Flash-DMA Primitive

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h`
- Modify: `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c`
- Test: `firmware/tests/test_lt7680_gfx.c` if the offline driver exposes a matching stub; otherwise record this as target-only coverage.

**Interfaces:**
- Consumes: existing `write_reg()`, `wr13()`, `wr32le()`, `wait_2d_idle()`, SFCS1 selection, and validated `lt7680_flash_read()` configuration.
- Produces:
  ```c
  lt7680_status_t lt7680_flash_dma_to_sdram(uint32_t flash_address,
                                             uint32_t sdram_base,
                                             uint16_t width_bytes,
                                             uint16_t height,
                                             uint16_t destination_stride_pixels);
  ```

- [ ] **Step 1: Define the DMA register constants and API contract.**

  Use the LT7680 register map:

  ```c
  #define LT7680_REG_DMA_CTRL 0xB6u
  #define LT7680_REG_DMA_SSTR 0xBCu
  #define LT7680_REG_DMA_DX   0xC0u
  #define LT7680_REG_DMA_DY   0xC2u
  #define LT7680_REG_DMA_WTH  0xC6u
  #define LT7680_REG_DMA_HIGH 0xC8u
  #define LT7680_REG_DMA_SWTH 0xCAu
  ```

  The API accepts a Flash byte address and an SDRAM canvas base. LT7680 DMA
  `DX/DY` are window coordinates, not absolute SDRAM addresses, so the helper
  saves `CVSSA` and `CVS_IMWTH`, temporarily points the canvas at `sdram_base`,
  performs the transfer at `(0,0)`, then restores the saved canvas settings.
  It rejects zero dimensions, addresses outside the 24-bit Flash range,
  transfers larger than the DMA width/height registers can represent, and
  SDRAM destinations below `0x200000`.

- [ ] **Step 2: Implement the SFCS1 DMA setup.**

  Before starting the DMA, write the Flash controller configuration used by
  the verified device:

  ```text
  B7 = 0xC0   (SFCS1 + host/DMA serial-flash mode, 24-bit address, standard mode)
  B9 = 0x3C   (SFCS1, inactive CS, SPI mode 0)
  BB = 0x0F   (validated conservative divisor)
  ```

  Program source address at `BC..BF`, destination X/Y at `C0..C3` as zero,
  block width at `C6..C7`, block height at `C8..C9`, and source width/stride at
  `CA..CB`. Start with `B6=0x01`, then poll core busy/status until completion
  or the existing bounded timeout expires. Restore the saved `CVSSA`,
  `CVS_IMWTH`, `B9=0x0C`, and leave B7 at SFCS1.

- [ ] **Step 3: Build and run static checks.**

  Run:

  ```sh
  cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
  cd firmware && ./tests/run_tests.sh
  cd .. && node sim/verify.js
  git diff --check
  ```

- [ ] **Step 4: Commit the DMA primitive.**

  ```sh
  git add KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c
  git commit -m "feat: add SFCS1 flash DMA primitive"
  ```

### Task 2: Add One-Glyph SDRAM Staging Probe

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`
- Modify: `KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h` only if a readback helper is needed.
- Modify: `docs/2026-08-20-rif-hardware-acceleration-design.md` after hardware result.

**Interfaces:**
- Consumes: `s_rif_image`, `rif_reader_find_glyph()`, `lt7680_flash_dma_to_sdram()`, and `lt7680_gfx_peek_pixel()`.
- Produces: a one-shot boot diagnostic line `RIF DMA probe ...` and a boolean that prevents cache/render activation until the probe passes.

- [ ] **Step 1: Resolve one known glyph without changing the visible renderer.**

  After `rif_init()` succeeds and while display output is disabled, resolve the
  digit glyph for character `'8'`. Read its directory entry and validate the
  expected RIF dimensions `64x128`, RGB565 stride `128` bytes, and payload size
  `16384` bytes.

- [ ] **Step 2: Transfer one bounded probe region.**

  Transfer only the first four source rows to SDRAM staging address `0x200000`:

  ```c
  lt7680_flash_dma_to_sdram(tile.offset, 0x200000u, 128u, 4u, 320u);
  ```

  Do not enable the accelerated renderer yet.

- [ ] **Step 3: Validate staging data and log the result.**

  Read a small set of staging pixels using the LT7680 memory readback helper or
  the existing diagnostic path. Print:

  ```text
  RIF DMA probe status=XX offset=XXXXXXXX sample=XXXX checksum=XXXXXXXX
  ```

  Mark the probe successful only if the transfer completes and at least one
  sample is not the all-zero/all-`0xFFFF` sentinel. If the result is invalid,
  keep the current GE/Flash path active.

- [ ] **Step 4: Build and flash the probe image.**

  Run the authoritative build and flash sequence:

  ```sh
  cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
  openocd -f openocd.cfg -c "init" -c "halt" \
    -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" \
    -c "reset" -c "shutdown"
  ```

  Monitor `/dev/ttyACM0` at 115200 and record the RIF DMA probe line.

- [ ] **Step 5: Commit only after hardware staging passes.**

  ```sh
  git add KEITHLEY_2000_LCD/Core/Src/main.c docs/superpowers/specs/2026-08-20-rif-hardware-acceleration-design.md
  git commit -m "test: validate RIF flash DMA staging"
  ```

### Task 3: Build a Bounded Framebuffer-Oriented Tile Cache

**Files:**
- Create: `KEITHLEY_2000_LCD/Core/Inc/rif_tile_cache.h`
- Create: `KEITHLEY_2000_LCD/Core/Src/rif_tile_cache.c`
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`
- Modify: CubeMX project source list if CMake does not auto-discover the new files.

**Interfaces:**
- Consumes: `rif_tile_t`, `lt7680_flash_dma_to_sdram()`, RIF identity `(kind, code)`, and staging base `0x200000`.
- Produces:
  ```c
  typedef struct {
      uint32_t kind;
      uint16_t code;
      uint32_t address;
      uint16_t stride;
      uint16_t width;
      uint16_t height;
      uint8_t ready;
  } rif_tile_cache_entry_t;

  void rif_tile_cache_init(void);
  lt7680_status_t rif_tile_cache_prepare(uint32_t kind, uint16_t code,
                                          const rif_tile_t *tile,
                                          rif_tile_cache_entry_t *entry);
  ```

- [ ] **Step 1: Reserve and validate cache memory.**

  Reserve a bounded region beginning at `0x300000`, leaving `0x200000` for
  staging. Each large glyph slot is a `128x64` RGB565 rectangle written with
  the configured canvas stride; half-height slots are `64x32`. Reject a slot
  if it would overlap either canvas page or exceed the configured SDRAM bound.

- [ ] **Step 2: Implement bounded tile construction.**

  Use the DMA staging area for one RIF tile at a time. Do not allocate two full
  tiles in STM32 RAM. First run the BTE memory-access-direction probe with a
  `2x3` asymmetric test rectangle. If the controller can map source block
  direction to the required transpose, use that verified BTE operation to
  produce the `128x64` cache slot. If it cannot, divide the source tile into
  `8x8` blocks (8 blocks across by 16 blocks down), DMA one block at a time into
  staging, write its transpose into the corresponding cache block with the
  graphic R/W cursor, and repeat until the complete tile is built. Record the
  cache entry only after all 128 blocks are written and verified.

- [ ] **Step 3: Add cache identity and reuse.**

  Key entries by the complete RIF `(kind, code)` identity. Reusing an entry
  must not reread U5 or rebuild the tile. A failed build invalidates the entry
  and returns an error to the renderer.

- [ ] **Step 4: Run host/build checks.**

  Run:

  ```sh
  cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
  cd firmware && ./tests/run_tests.sh
  cd .. && node sim/verify.js && git diff --check
  ```

- [ ] **Step 5: Commit the cache module.**

  ```sh
  git add KEITHLEY_2000_LCD/Core/Inc/rif_tile_cache.h KEITHLEY_2000_LCD/Core/Src/rif_tile_cache.c KEITHLEY_2000_LCD/Core/Src/main.c
  git commit -m "feat: cache RIF glyph tiles in SDRAM"
  ```

### Task 4: Switch External Glyph Rendering to BTE

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`
- Modify: `KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h` only if the blit API needs a validated extension.
- Modify: `docs/superpowers/specs/2026-08-20-rif-hardware-acceleration-design.md` with orientation result.

**Interfaces:**
- Consumes: `rif_tile_cache_prepare()`, `lt7680_gfx_blit()`, `s_render_page`, `panel_transform_ui_to_fb()`.
- Produces: BTE-first `ui_draw_external_digits()` with fallback to the existing GE/Flash implementation.

- [ ] **Step 1: Add the cache path behind a disabled feature flag.**

  Add a compile-time default-off flag for the complete renderer. When disabled,
  behavior must remain the current external GE/Flash path.

- [ ] **Step 2: Blit one cached large glyph.**

  For a UI glyph position `(x, y)`, map its top-left through the existing
  transform and call:

  ```c
  lt7680_gfx_blit(s_render_page, entry.address, entry.stride,
                  fb_x, fb_y, entry.width, entry.height);
  ```

  The large glyph uses `128x64` framebuffer dimensions after transpose. Do not
  add an axis reversal or alter panel timing.

- [ ] **Step 3: Keep special colors on fallback.**

  Use BTE only for the normal green RIF foreground/background pair. For red,
  muted, missing, invalid, or cache-not-ready glyphs, invoke the existing
  GE/Flash renderer and report no accelerated success.

- [ ] **Step 4: Build and run static checks.**

  Run the Release build, host test suite, simulator verification, and
  `git diff --check`. Confirm RAM remains below 20 KiB after the cache table and
  staging buffers are linked.

- [ ] **Step 5: Commit the renderer switch.**

  ```sh
  git add KEITHLEY_2000_LCD/Core/Src/main.c docs/superpowers/specs/2026-08-20-rif-hardware-acceleration-design.md
  git commit -m "perf: render RIF glyphs with BTE"
  ```

### Task 5: Hardware Acceptance and Cleanup

**Files:**
- Modify: `docs/superpowers/specs/2026-08-20-rif-hardware-acceleration-design.md`
- Modify: `docs/2026-08-19-rif-miso-diagnosis.md` only if the final hardware result changes its status.

**Interfaces:**
- Consumes: authoritative Release ELF, `/dev/ttyACM0`, OpenOCD, and the staged diagnostic logs.
- Produces: recorded DMA/cache/BTE acceptance or a documented fallback blocker.

- [ ] **Step 1: Flash the staged acceptance build.**

  Use the known halt-first DAPLink command and monitor at 115200:

  ```sh
  stty -F /dev/ttyACM0 115200 cs8 -cstopb -parenb raw -echo
  openocd -f openocd.cfg -c "init" -c "halt" \
    -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" \
    -c "reset" -c "shutdown"
  timeout 30s dd if=/dev/ttyACM0 bs=1 status=none
  ```

- [ ] **Step 2: Verify the staged serial and visual milestones.**

  Require:

  ```text
  RIF JEDEC=EF4017
  RIF external digits ready
  RIF DMA probe status=0x00
  ```

  Confirm the diagnostic glyph is nonzero, upright, unmirrored, and has the
  expected RGB565 colors before enabling the full BTE path.

- [ ] **Step 3: Measure the final performance.**

  Collect at least five `PERF` lines. Acceptance requires `frame-ms < 33`, fps
  near 30, and `missed=0`. If the cache is cold, record cold-build time
  separately from steady-state frame time.

- [ ] **Step 4: Remove temporary diagnostics only after acceptance.**

  Keep the stable JEDEC/RIF readiness logs and fallback errors. Remove only the
  SFCS comparison and DMA probe verbosity that is no longer needed, then rerun
  the complete static checks.

- [ ] **Step 5: Commit the acceptance record.**

  ```sh
  git add docs/superpowers/specs/2026-08-20-rif-hardware-acceleration-design.md docs/2026-08-19-rif-miso-diagnosis.md
  git commit -m "docs: record RIF hardware acceleration acceptance"
  ```
