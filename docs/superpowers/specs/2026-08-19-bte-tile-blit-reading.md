# BTE Tile-Blit Reading Rendering

Date: 2026-08-19
Status: Approved for implementation

## Problem

The reading path draws 500 Hz Demo readings at 95–155 ms/frame (8–11 fps),
3–4× over the 30 Hz / 33 ms budget. The cost is `ui_draw_external_digits`,
which streams each RGB565 RIF tile from SPI flash in 32-pixel chunks and issues
one GE `fill_rect` per color run. A single reading snapshot (~7 glyphs at
64×128) becomes hundreds of blocking GE/SPI transactions plus dozens of flash
reads.

Trend was already excluded from runtime frames (only the initial frame draws
it), so the reading text path is now the sole remaining bottleneck.

## Goal

Render the primary reading and unit glyphs with one LT7680 BTE copy command
per glyph instead of hundreds of per-run GE fills, reaching ~30 Hz while the
500 Hz Demo feed continues.

## Approach

### Tile library in SDRAM

- Reserve a tile library at SDRAM address `0x200000` (beyond the two 1 MiB
  canvas pages at `0x000000` / `0x100000`).
- The library stores each glyph already **transposed to frame-buffer
  orientation** (128 wide × 64 tall RGB565), because BTE is an axis-aligned
  copy and cannot rotate the 64×128 UI-space tile.
- Layout uses the same 320-pixel row stride as the canvas, so BTE source and
  destination share one stride (source `S0_WTH=320`, target `DT_WTH=320`,
  `S0_X/Y` selects the tile slot). A slot is 128×64; 41 glyphs fit within the
  library area.
- Building the library happens once after RIF validation, before the first
  frame: read each RIF tile from flash, transpose in MCU RAM (a small static
  row buffer), write to SDRAM via the graphic R/W cursor + MRWDP. One
  cursor-positioning write per row, then each byte is an independent CS
  transaction (LT7680A-R MRWDP constraint); ~656 KiB ≈ <1 s one-time cost.

### Frame-buffer blit

New `lt7680_gfx_blit()` generalizes the verified BTE copy ROP
(`BTE_CTRL1=0xC2`, `BTE_COLR=0x25`):

- Source: absolute SDRAM address (tile slot), source stride 320, `S0_X/Y` = slot.
- Destination: current canvas page, `DT_WTH=320`, `DT_X/Y` = fb position.
- Size: 128×64 for digit glyphs, 64×32 for half-height glyphs.
- The glyph background (black) equals `MAIN_DISPLAY_COLOR_BG`, so a plain copy
  is safe; no ROP transparency is needed.

### Renderer integration

- `ui_draw_external_digits` becomes BTE-first: resolve the glyph tile from the
  RIF directory, blit it in one command per glyph, `wait_2d_idle`, and advance.
- Glyphs not present in the library (or when the library build failed) fall
  back to the existing per-run path unchanged.
- Because the Demo `special=0` keeps `value_color=GREEN` (= RIF foreground),
  BTE copy color is correct. Special colors (red/muted) continue to use the
  fallback path; they are rare and not part of the 30 Hz measurement.

## Verification

- Host tests + `node sim/verify.js` stay green.
- Offline skeleton build (`firmware/`) and CubeMX Debug/Release build stay
  within 64 KiB Flash / 20 KiB RAM.
- Hardware: PERF fps rises toward 30 with trend still excluded; reading still
  correct and the "wipe" effect is gone.

## Non-goals

- No U5 writes; protected backups untouched.
- No SPI-mode register changes.
- Trend remains initial-frame-only until reading hits 30 Hz.