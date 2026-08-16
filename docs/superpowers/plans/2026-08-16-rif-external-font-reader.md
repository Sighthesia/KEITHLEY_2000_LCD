# RIF External Font Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render Keithley 2000 primary-reading glyphs from the verified U5 RIF resource image through the LT7680 serial-flash controller.

**Architecture:** The LT7680 graphics driver exposes a conservative polled raw-flash read operation and JEDEC probe. A byte-identical `rif_reader` module validates and searches the fixed RIF header/directory without dynamic allocation. The CubeMX renderer streams resolved RGB565 tiles to the hidden SDRAM page; it uses `font_text` as a visible fallback if external resources are unavailable, allowing the 64x128 built-in digit bitmap data to be removed from MCU Flash.

**Tech Stack:** C11, STM32F103C8T6, STM32 HAL, LT7680A-R serial SPI-master FIFO, Winbond W25Q64JV, CMake/Ninja, host GCC tests.

## Global Constraints

- U5 is a W25Q64JV detected as JEDEC `EF 40 17`; read only, never program it in this work.
- U5 is controlled through LT7680 registers `B8h`-`BBh`, not STM32 GPIO.
- Use raw W25Q Read Data `03h`, SPI mode 0, SFCS0, `SPI_DIVSOR=0x0F` for first bring-up.
- Deassert `SS_ACTIVE` on every success and failure path; never allow either 16-byte FIFO to overflow.
- Do not use GTFNT/CGRAM automatic font mode or assume RIF is a hardware font-ROM format.
- Shared pure logic under `firmware/src` and `KEITHLEY_2000_LCD/Core/{Src,Inc}` must be byte-identical.
- U5 tile data is RGB565 little-endian; panel coordinates retain the existing pure transpose transform.
- Do not buffer a complete 16 KiB 64x128 tile in STM32 RAM; use bounded streaming chunks.
- Remove the 64x128 built-in digit font from the target to recover Flash capacity. On external resource failure, show the primary reading with the existing 12x24 `font_text` instead.
- Preserve all existing reset order, panel initialization, double-buffer presentation, BTE behavior, and UART receive scheduling.
- Final target must fit STM32F103C8T6 limits: 64 KiB Flash and 20 KiB RAM.

---

### Task 1: Add a Testable RIF Directory Reader

**Files:**
- Create: `firmware/src/rif_reader.h`
- Create: `firmware/src/rif_reader.c`
- Create: `firmware/tests/test_rif_reader.c`
- Modify: `firmware/tests/run_tests.sh`
- Create: `KEITHLEY_2000_LCD/Core/Inc/rif_reader.h`
- Create: `KEITHLEY_2000_LCD/Core/Src/rif_reader.c`
- Modify: `KEITHLEY_2000_LCD/CMakeLists.txt`

**Interfaces:**
- Consumes: 64-byte RIF header and 48-byte directory records produced by `tools/rif_common.py`.
- Produces: `rif_reader_parse_header()`, `rif_reader_parse_entry()`, and `rif_reader_find_glyph()` for the target renderer.

- [ ] **Step 1: Write failing parser tests**

Create `firmware/tests/test_rif_reader.c` with a minimal valid v1 header and
entry byte arrays. Assert a valid `K2RF` header yields image size, directory
count, directory offset, payload offset, and base. Assert malformed magic,
version, image range, non-4KiB base, invalid directory range, invalid entry
alignment, invalid RGB565 stride, and a missing glyph identity are rejected.

```c
assert(rif_reader_parse_header(header, sizeof(header), &image) == RIF_OK);
assert(rif_reader_find_glyph(&entry, RIF_KIND_DIGIT_CHAR, '8', &tile) == RIF_OK);
assert(rif_reader_parse_header(bad_magic, sizeof(bad_magic), &image) == RIF_ERR_FORMAT);
```

- [ ] **Step 2: Run the parser test to verify it fails**

Run: `cd firmware && ./tests/run_tests.sh`

Expected: compilation fails because `rif_reader.h` does not exist.

- [ ] **Step 3: Define the allocation-free public contract**

Create `rif_reader.h` with fixed-format constants and structs that contain no
pointers into transient input buffers:

```c
typedef enum { RIF_OK = 0, RIF_ERR_PARAM, RIF_ERR_FORMAT, RIF_ERR_RANGE } rif_status_t;
typedef struct { uint32_t flash_base, image_size, directory_offset, payload_offset;
                 uint16_t directory_count, directory_entry_size; } rif_image_t;
typedef struct { uint16_t kind, id, width, height, stride, code;
                 uint32_t offset, size, crc32; } rif_entry_t;
typedef struct { uint32_t offset, size; uint16_t width, height, stride; } rif_tile_t;
rif_status_t rif_reader_parse_header(const uint8_t *data, uint16_t len, rif_image_t *out);
rif_status_t rif_reader_parse_entry(const rif_image_t *image, const uint8_t *data,
                                    uint16_t len, rif_entry_t *out);
rif_status_t rif_reader_find_glyph(const rif_entry_t *entry, uint16_t kind,
                                   uint16_t code, rif_tile_t *out);
```

Use explicit little-endian decoders. Validate fixed v1 fields, image and
directory bounds, 4KiB offsets, and RGB565 `stride == width * 2` with
`size == stride * height`. Do not calculate CRC in target code yet; header and
directory structural validation is the boot gate, while tile transfer validates
length and uses fallback on a read failure.

- [ ] **Step 4: Run parser tests and mirror the pure logic**

Run: `cd firmware && ./tests/run_tests.sh`

Expected: `PASS test_rif_reader` and all existing tests pass.

Copy the two source files to `KEITHLEY_2000_LCD/Core/{Inc,Src}` and run:

```sh
diff -u firmware/src/rif_reader.h KEITHLEY_2000_LCD/Core/Inc/rif_reader.h
diff -u firmware/src/rif_reader.c KEITHLEY_2000_LCD/Core/Src/rif_reader.c
```

Add `src/rif_reader.c` to `SRCS` and `Core/Src/rif_reader.c` to the target
CMake source list.

- [ ] **Step 5: Commit parser deliverable**

```sh
git add firmware/src/rif_reader.[ch] firmware/tests/test_rif_reader.c \
  firmware/tests/run_tests.sh KEITHLEY_2000_LCD/Core/Inc/rif_reader.h \
  KEITHLEY_2000_LCD/Core/Src/rif_reader.c KEITHLEY_2000_LCD/CMakeLists.txt
git commit -m "feat: add RIF directory reader"
```

### Task 2: Implement LT7680 Raw Serial-Flash Reads

**Files:**
- Modify: `firmware/src/lt7680_gfx.h`
- Modify: `firmware/src/lt7680_gfx.c`
- Modify: `firmware/tests/test_lt7680_gfx.c`
- Modify: `KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h`
- Modify: `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c`

**Interfaces:**
- Consumes: existing `lt7680_write_reg()` / `lt7680_read_reg()` operations.
- Produces: `lt7680_flash_read()` and `lt7680_flash_read_jedec_id()` for RIF loading.

- [ ] **Step 1: Extend host tests for parameter rejection**

Add assertions that reject a null output buffer, zero-length read, and a
24-bit-address overflow before any bus transaction:

```c
uint8_t id[3];
assert(lt7680_flash_read(0u, 0, 1u) == LT7680_ERR_PARAM);
assert(lt7680_flash_read(0u, id, 0u) == LT7680_ERR_PARAM);
assert(lt7680_flash_read(0x1000000u, id, 1u) == LT7680_ERR_PARAM);
```

- [ ] **Step 2: Run tests to establish the missing-API failure**

Run: `cd firmware && ./tests/run_tests.sh`

Expected: compilation fails because the flash-read APIs are absent.

- [ ] **Step 3: Add the driver APIs and bounded FIFO transaction**

Declare:

```c
lt7680_status_t lt7680_flash_read(uint32_t address, uint8_t *data, uint16_t length);
lt7680_status_t lt7680_flash_read_jedec_id(uint8_t id[3]);
```

In each graphics driver tree, define `SFL_CTRL=0xB7`, `SPIDR=0xB8`,
`SPIMCR2=0xB9`, `SPIMSR=0xBA`, and `SPI_DIVSOR=0xBB`. Use helpers that wait
for TX space, wait for idle (`SPIMSR` TX-empty and idle bits), pop RX FIFO, and
always write `SPIMCR2=0x0C` in a single cleanup path.

For reads, issue `03h, A23, A15, A7`, then at most 12 dummy clocks per FIFO
batch. Drain exactly the number of transferred RX bytes, discard the first four
command/address bytes, and append later RX bytes to the caller buffer. For
JEDEC use `9Fh` plus three dummy clocks and return RX bytes 1-3. Set divisor
`0x0F` before each bring-up transaction and use mode-0 control `0x1C`.

- [ ] **Step 4: Run host and target builds**

Run:

```sh
cd firmware && ./tests/run_tests.sh && make
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
```

Expected: all host tests pass; target compile succeeds. Treat hardware JEDEC
as unverified until Task 4.

- [ ] **Step 5: Commit serial-flash driver deliverable**

```sh
git add firmware/src/lt7680_gfx.[ch] firmware/tests/test_lt7680_gfx.c \
  KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c
git commit -m "feat: read serial flash through LT7680"
```

### Task 3: Integrate RIF Lookup and External Tile Rendering

**Files:**
- Modify: `KEITHLEY_2000_LCD/Core/Src/main.c`
- Modify: `KEITHLEY_2000_LCD/Core/Src/font_digits.c`
- Modify: `KEITHLEY_2000_LCD/Core/Inc/font_digits.h`
- Modify: `firmware/src/font_digits.c`
- Modify: `firmware/src/font_digits.h`
- Modify: `sim/index.html`
- Modify: `sim/font_data.js`
- Modify: `sim/verify.js`

**Interfaces:**
- Consumes: `rif_reader_*()` and `lt7680_flash_read()`.
- Produces: primary-reading external tile rendering with `font_text` fallback.

- [ ] **Step 1: Add host-visible fallback tests**

Extend `firmware/tests/test_font_digits.c` to assert that the replacement API
maps supported primary-reading byte identities and UTF-8 symbols to RIF kind and
code values, and that unknown glyphs are rejected. Update simulator checks to
assert that its display remains geometrically unchanged when the external-font
feature flag is unavailable.

```c
assert(font_digit_rif_code('8', &kind, &code));
assert(font_digit_rif_code('\0', &kind, &code) == false);
```

- [ ] **Step 2: Run tests to verify the mapping API is absent**

Run: `cd firmware && ./tests/run_tests.sh`

Expected: compilation fails until the mapping interface is implemented.

- [ ] **Step 3: Replace static 64x128 data with identity mapping**

Remove generated 64x128 bitmap arrays from both mirror trees and keep only the
small character/symbol identity mapping needed to locate RIF entries. Preserve
the public digit width/height constants so existing formatting and layout stay
unchanged. Regenerate `sim/font_data.js` only if its source contract changes;
the browser preview may continue to use its generated data because it is not
target Flash.

Implement boot-time `rif_external_init()` in CubeMX `main.c`:

1. read JEDEC and log the three bytes;
2. read the 64-byte header at base 0;
3. parse it; reject failures and retain text fallback;
4. read directory records as requested, not all into RAM.

Extend the existing resumable bitmap job with an external-tile mode. Resolve
one glyph entry before drawing it, then stream RGB565 data in a fixed small
buffer (no more than 64 bytes) from U5. Map each source tile pixel through the
existing UI-to-framebuffer transpose and issue bounded output operations only
on the hidden page. Do not mark the render item complete until its tile has
fully transferred. On any lookup/read error, clear job state and render the
same text using `ui_draw_text()` at a legible 12x24 position.

- [ ] **Step 4: Build, inspect size, and synchronize shared files**

Run:

```sh
cd firmware && ./tests/run_tests.sh && make
node sim/verify.js
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
arm-none-eabi-size KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf
diff -u firmware/src/font_digits.c KEITHLEY_2000_LCD/Core/Src/font_digits.c
diff -u firmware/src/font_digits.h KEITHLEY_2000_LCD/Core/Inc/font_digits.h
```

Expected: host and simulator checks pass; target is below 64 KiB Flash and 20
KiB RAM. Confirm the removal of the generated 64x128 target font saves enough
space for RIF code and diagnostic logging.

- [ ] **Step 5: Commit renderer deliverable**

```sh
git add KEITHLEY_2000_LCD/Core/Src/main.c \
  KEITHLEY_2000_LCD/Core/{Inc,Src}/font_digits.[ch] \
  firmware/src/font_digits.[ch] firmware/tests/test_font_digits.c \
  sim/index.html sim/font_data.js sim/verify.js
git commit -m "feat: render primary reading from RIF tiles"
```

### Task 4: Verify Hardware Readout and Fallback

**Files:**
- Modify: `docs/superpowers/specs/2026-08-16-rif-external-font-read-design.md`
- Modify: `.trellis/spec/backend/quality-guidelines.md`

**Interfaces:**
- Consumes: built `KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`.
- Produces: recorded hardware acceptance result or a diagnostic blocker with
  internal-text fallback retained.

- [ ] **Step 1: Build the authoritative target image**

Run:

```sh
cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf
arm-none-eabi-size KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf
```

Expected: the only image selected for flashing is
`KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf`.

- [ ] **Step 2: Flash with the proven halt-first sequence**

Run:

```sh
openocd -f openocd.cfg \
  -c "init" -c "halt" \
  -c "program KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf verify" \
  -c "reset" -c "shutdown"
```

Expected: program verification succeeds. If the final reset command reports a
reset error after verification, press the board reset button and continue with
serial observation; do not reprogram U5.

- [ ] **Step 3: Execute ordered serial and display acceptance**

At 115200 baud assert UART reports `JEDEC=EF4017`, RIF header validation, and
external-font enabled. Then check the visible sequence: diagnostic tile color
order, external glyph `8`, and a live primary reading. Confirm it is upright,
not mirrored, and has the existing 64x128 layout. Force or temporarily bypass
the RIF readiness gate and confirm the same reading remains visible in 12x24
fallback text.

- [ ] **Step 4: Record results and commit documentation**

Update both documents with the observed JEDEC result, SPI divisor, visual
result, and fallback result. Do not record unverified results as passed.

```sh
git add docs/superpowers/specs/2026-08-16-rif-external-font-read-design.md \
  .trellis/spec/backend/quality-guidelines.md
git commit -m "docs: record RIF font hardware validation"
```
