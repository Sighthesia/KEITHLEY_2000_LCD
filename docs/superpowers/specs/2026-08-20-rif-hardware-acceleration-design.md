# RIF Hardware-Accelerated Glyph Rendering

Date: 2026-08-20
Status: DMA staging blocked on hardware; renderer switch remains default-off

## Goal

Replace the current external-font rendering path, which takes about 11–12
seconds per frame, with LT7680 hardware-assisted resource transfer and BTE
glyph blits. The target is stable reading rendering near 30 fps without
blocking the 500 Hz sample feed.

## Verified Hardware Facts

- U5 responds through LT7680 `SFCS1`, not `SFCS0`.
- JEDEC read is stable as `00 EF 40 17`; parsed ID is `EF4017`.
- RIF header at U5 address zero is valid.
- U5 is read-only for this work. No write-enable, program, erase, QE, or
  status-register modification is permitted.
- Current external glyph path reads RIF rows and issues many synchronous GE
  `fill_rect` operations. The observed external-font frame is approximately
  11–12 seconds.
- LT7680 BTE rectangle blit primitive is already available as
  `lt7680_gfx_blit()` and has passed target compilation.
- The two display canvas pages occupy SDRAM `0x000000` and `0x100000`.
- Hardware acceptance on 2026-08-20: JEDEC and RIF header passed, but the
  first DMA staging probe failed with `status=0x02`, `offset=0x00021000`, and
  a black pixel sample. Full BTE rendering remains unaccepted.

## Architecture

```text
U5 / SFCS1
  -> LT7680 SPI Master Flash DMA
  -> SDRAM staging area
  -> one-time tile transpose/cache build
  -> SDRAM framebuffer-oriented glyph tile
  -> one BTE blit per glyph
  -> current canvas page
```

### Flash Transfer

Add a conservative LT7680 SPI Flash DMA-to-SDRAM interface using the existing
SFCS1 selection and validated Flash configuration. The first implementation
must expose a single bounded transfer operation and report completion or
timeout. It must not expose any Flash write operation.

The first hardware test transfers one known RIF digit tile to a staging area;
the complete reading renderer is not switched until this test has proved that
the staging contents are nonzero and stable.

### Tile Cache

RIF large glyphs are stored as UI-space `64x128` RGB565 tiles. The framebuffer
orientation is the verified pure transpose:

```text
fb_x = ui_y
fb_y = ui_x
```

The cache therefore stores large glyphs as `128x64` RGB565 tiles. The cache is
located outside both display canvas pages and must be bounded by the actual
SDRAM configuration. A tile entry records its RIF identity, cache address,
stride, dimensions, and ready state.

The renderer destination uses the existing pure transpose without axis
reversal: the UI top-left is mapped with `panel_transform_ui_to_fb()`, and a
validated cache entry is blitted as `128x64` framebuffer pixels. The complete
BTE renderer remains compile-time opt-in; the default build keeps the existing
GE/Flash renderer.

The MCU must not allocate a complete 16 KiB input tile and a second complete
16 KiB output tile simultaneously. The transpose/cache builder uses bounded
staging buffers or a verified hardware transfer arrangement.

### Rendering

`ui_draw_external_digits()` becomes BTE-first after the cache is ready:

- resolve the RIF glyph identity;
- ensure the glyph cache entry is ready;
- issue one `lt7680_gfx_blit()` at the framebuffer-transformed destination;
- advance to the next glyph;
- retain the existing GE/Flash path as fallback when DMA, cache build, or BTE
  validation fails.

Special-color glyphs may continue to use the fallback path until a separate
colorized cache policy is verified. The first performance target covers the
normal green reading path.

## Safety and Failure Handling

- Keep display output disabled during the first DMA/staging probe.
- Never write to U5.
- Never place staging or cache data over SDRAM canvas page 0 or page 1.
- Verify all DMA source, destination, width, height, and stride ranges before
  starting a transfer.
- On timeout, invalid data, cache overflow, or orientation mismatch, disable
  the accelerated path and use the existing internal/external fallback.
- Do not enable Quad/Dual Flash commands or hardware font-ROM mode as part of
  this change.

## Verification

### Static

- Release target build remains below 64 KiB Flash and 20 KiB RAM.
- Host tests pass.
- `node sim/verify.js` passes.
- `git diff --check` passes.

### Hardware

The serial log must continue to show:

```text
RIF JEDEC=EF4017
RIF external digits ready
```

The first DMA probe must report a nonzero staging checksum or pixel sample.
The diagnostic glyph must be upright, unmirrored, and use the expected RGB565
foreground/background. Only then may the complete BTE reading path be enabled.

The final performance acceptance is:

```text
frame-ms < 33
fps near 30
missed=0
```

Current blocker:

```text
RIF DMA probe status=02 offset=00021000 sample=0000 checksum=117697CD
```

The next diagnostic must distinguish DMA start/status failure, destination
window addressing failure, and a false-negative SDRAM readback sample before
changing the cache or renderer implementation.

## Non-Goals

- No U5 programming or resource-image rewrite.
- No change to the validated panel timing, framebuffer transpose, or reset
  order.
- No full trend-rendering redesign in this work.
- No removal of the fallback renderer until hardware acceleration has passed
  the staged acceptance tests.
