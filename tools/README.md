# K2000 Resource Flash Image (RIF)

Host-side tools that produce and verify a **resource content image** for the
LT7680-attached `W25Q128JV` serial flash (U5 on the K2000 display board).
The W25Q128 hangs off the LT7680 SPI (U2.20-23), **not** the STM32, so the
STM32 firmware cannot write it directly. This toolchain only generates and
validates the *content*; programming the device is a separate step (LT7680
serial-flash programming interface or an external chip programmer).

First payload: the generated big-digit font (`font_digits`) and half-height
suffix font (`font_half`) pre-rendered as **RGB565 glyph tiles**, plus a
diagnostic color tile and reserved regions for a future text font and UI
assets.

## Design rules

- **Deterministic, host-only.** The packer parses the generated C arrays in
  `firmware/src` (`font_digits.c/.h`, `font_half.c/.h`) with the same
  extraction used by `tools/make_sim_font.py`. It never executes firmware and
  never touches hardware.
- **Bit/row order preserved.** The 1bpp source is row-packed MSB-first
  (bit set = ink). Tiles keep row 0 = top, MSB = left pixel.
- **Explicit palette.** RGB565 foreground/background values are chosen at
  pack time (default neon green `#00FF33` on black) and recorded per entry.
  Every 16-bit pixel is stored little-endian (low byte first), matching the
  LT7680 MRWDP pixel byte order, so tiles can be streamed to display RAM
  without byte swaps.
- **4 KiB alignment everywhere.** W25Q128JV sector size. Every payload and
  every reserved region starts on a 4 KiB boundary; reserved regions are a
  multiple of 4 KiB, pre-filled with the fill byte (default `0xFF`, the
  erased-flash state) so later content can be programmed in place after a
  sector erase.
- **Self-describing and checkable.** Header + directory + per-entry CRC32 +
  whole-image CRC32. `verify_resource_flash.py` validates a file or a flash
  dump without any hardware.
- **Original flash contents are NOT assumed.** The W25Q128 on a harvested
  board may contain the original firmware's resource data (the repository
  only has a 200704-byte `Flash Data_K2000 Display Board TFT_V15.bin` dump
  whose internal layout is not documented). Writing this image **overwrites**
  the sectors it occupies. Back up first (see Safety workflow) and choose
  `--base-offset` deliberately.

## Default flash map

Base address default `0x000000`; absolute flash address = base + file offset.
`--base-offset` moves the whole image (recorded in the header).

| Flash addr (base 0x000000) | File offset | Size | Content |
| --- | --- | --- | --- |
| `0x000000` | `0x000000` | `0x40` | Header: magic `K2RF`, version 1.0, image size, CRC32 fields, directory pointer, intended flash base, fill byte |
| `0x000040` | `0x000040` | `0x840` | Directory: 44 entries x 48 B (38 digit glyphs, 3 half glyphs, diagnostic, 2 reserved) |
| `0x000880` | `0x000880` | `0x780` | Padding (fill byte) |
| `0x001000` | `0x001000` | `0x8C000` | `font_digits` chars `0123456789.+-Ee%mukKMWVOhDAC?RFLHzs`: 35 tiles x 16 KiB (64x128 RGB565) |
| `0x08D000` | `0x08D000` | `0xC000` | `font_digits` symbols MICRO / DEGREE / OHM: 3 tiles x 16 KiB |
| `0x099000` | `0x099000` | `0x3000` | `font_half` chars `DCA`: 3 tiles x 4 KiB (32x64 RGB565) |
| `0x09C000` | `0x09C000` | `0x2000` | Diagnostic 64x64 RGB565 color tile (8 vertical bands) |
| `0x09E000` | `0x09E000` | `0x10000` | Reserved: `font_text` (future 12x24 text font) |
| `0x0AE000` | `0x0AE000` | `0x10000` | Reserved: `ui_assets` (future UI assets) |
| `0x0BE000` | - | - | End of image (778,240 bytes = 0xBE000) |

Sectors written: `0x000000..0x0BDFFF` (190 x 4 KiB sectors at the default
base). Everything outside this range is untouched.

## Commands

```sh
# Build the default image (base 0x000000, neon green on black)
python3 tools/pack_resource_flash.py

# Build at a different base / palette / output path
python3 tools/pack_resource_flash.py \
    --base-offset 0x200000 \
    --fg '#00FF33' --bg '#000000' \
    --output build/resource/k2000_resource_v1_0x200000.img

# Verify an image (structural checks: size, magic/version, CRCs, directory
# bounds, alignment, glyph counts, reserved fill, diag colors)
python3 tools/verify_resource_flash.py build/resource/k2000_resource_v1_0x000000.img

# Verify + cross-check against the generated font sources (exact glyph
# counts, char codes, payload sizes, and RGB565 -> 1bpp round trip)
python3 tools/verify_resource_flash.py --src-dir firmware/src \
    build/resource/k2000_resource_v1_0x000000.img

# Host unit tests
tools/tests/run_tests.sh
```

The packer always runs structural verification before writing and prints the
full flash map. Exit codes: 0 = valid, 1 = problems found.

## Safety / verification workflow

1. **Back up the current W25Q128 contents first.** Dump the whole 16 MB (or
   at least the sector range the image will occupy) through the LT7680
   serial-flash read interface or a chip programmer, and keep the dump
   off-board. The original layout is not documented; this backup is the only
   way to restore it.
2. **Choose the base.** The default `0x000000` overwrites whatever is there.
   If original resource data must survive, place the image at a free offset
   with `--base-offset` and confirm the range is not in use.
3. **Erase exactly the image range** `[base, base + image_size)` in 4 KiB
   sectors, then program the image bytes. Never program without a prior
   sector erase, and never program outside the documented range.
4. **Read back and verify on the host.** Dump the programmed range and run
   `verify_resource_flash.py --src-dir firmware/src <dump>`; it checks
   header/image CRCs, per-entry CRCs, glyph counts, and round-trips every
   tile back to the 1bpp source bitmaps. Any byte error or wrong placement
   fails the check. A full-chip dump is accepted too when the image starts at
   the recorded base (the default `0x000000`); for a non-zero base, dump
   `[base, base + image_size)` instead.
5. **Visual check on hardware.** Display the diagnostic tile; the eight
   vertical bands must read RED, GREEN, BLUE, WHITE, BLACK, FG, BG, CYAN in
   order, proving flash readback, RGB565 rendering and orientation in one
   step. Then render a known glyph (e.g. `8`) from the image and compare
   against the simulator.

## Binary format summary

Header (64 B): `K2RF` magic, version 1.0, `image_size` (u32 LE),
`header_crc32` (u32 LE), `dir_offset`, `dir_size`, `dir_count` (u16),
`entry_size` = 48 (u16), `payload_start`, `image_crc32`, `flash_base`
(u32 LE), `fill_byte`, 23 reserved bytes. Both CRC fields are computed with
**both CRC fields zeroed** (header CRC covers the 64 header bytes; image CRC
covers the whole file) - this is the definition the verifier reproduces.

Directory entry (48 B): `kind[4]` (`DGTC` digit char, `DGTS` digit symbol,
`HLFC` half char, `DIAG` diagnostic, `RSVD` reserved), `id` (u32), `offset`
(u32, file-relative, 4 KiB aligned), `size` (u32), `width` (u16), `height`
(u16), `stride` (u16 = width x 2), `code` (ASCII char or symbol id),
`flags` (u8; bit0 pixel format 0 = RGB565 LE, bit1 origin 0 = top-left),
`crc32` (u32, of payload), `fg_rgb565` (u16), `bg_rgb565` (u16), `name[16]`
(ASCII, NUL padded).

CRC32 is `zlib.crc32` (CRC-32/ISO-HDLC, reflected, final XOR), stored
little-endian.

## Why direct LT7680 GTFNT use is deferred

The LT7680 has a built-in text-font engine (GTFNT, graphic text font) that
can draw characters from font data stored in the attached serial flash.
Using it for these fonts was considered and **deferred** for this release:

- **Unverified external-font container.** GTFNT external fonts require the
  flash to hold the LT7680's font-type-4 descriptor/tile layout, which has
  not been reverse-engineered or validated on this exact LT7680A-R silicon.
  The RIF format is documented, deterministic and verifiable on the host;
  GTFNT formatting is not.
- **Size and pixel control.** The UI needs 64x128 big-digit tiles and a
  32x64 half-height suffix font rendered with an exact palette and per-pixel
  semantics (neon green on black, no synthetic precision). GTFNT targets
  small text fonts and does not expose that control; the tiles are drawn
  through the existing, hardware-verified geometry-engine path instead.
- **Character set and symbols.** The digit font includes unit symbols
  (MICRO, DEGREE, OHM) and a `?` fallback that are handled as first-class
  glyphs here, with per-glyph CRCs and a round-trip check.

If GTFNT external fonts become wanted later, they can occupy a separate
flash region formatted per the LT7680 font-type-4 layout without disturbing
this image's reserved regions.

## Scope

The tools intentionally do **not** modify the STM32 firmware, the LT7680
driver, or the simulator. They only produce and verify content for the
resource flash. Do not commit the generated `.img` files; they live under
`build/` (git-ignored) and are reproducible from the font sources.