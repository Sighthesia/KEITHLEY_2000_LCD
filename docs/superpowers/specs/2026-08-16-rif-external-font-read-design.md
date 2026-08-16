# RIF External Font Read Design

Date: 2026-08-16
Status: Approved for implementation

## Goal

Use the RIF resource image already programmed at U5 address `0x000000` to
render the primary reading from external flash. U5 is a W25Q64JV detected as
JEDEC `EF 40 17`; its SPI signals are owned by the LT7680A-R, not the STM32.

## Scope

- Add a conservative raw serial-flash read path through the LT7680 SPI-master
  FIFO.
- Add a shared, allocation-free RIF header and directory parser.
- Use external RGB565 tiles only for the primary reading and unit glyphs.
- Preserve the internal fonts for all other UI text and as the failure fallback.
- Add UART diagnostics for JEDEC, RIF validation, and the selected font source.

This work does not rewrite U5, use LT7680 automatic GTFNT/CGRAM mode, alter
panel timing, or replace the existing BTE presentation path.

## Architecture

### LT7680 Serial Flash Access

`lt7680_flash_read()` drives the LT7680's SPI-master registers B8h-BBh. It
uses W25Q Read Data (`03h`) in SPI mode 0 and starts at a conservative divisor
of `0x0F` (about 2 MHz with the current 66.7 MHz core clock).

Each transaction:

1. Selects SFCS0 and asserts SS.
2. Sends `03h` plus a 24-bit address, then dummy bytes for the requested data.
3. Drains the 16-byte receive FIFO before it can overflow; command/address
   turnaround bytes are discarded.
4. Waits for TX-empty and idle, reports overflow or timeout, and always
   deasserts SS to reset the FIFOs.

`lt7680_flash_read_jedec_id()` is the first hardware probe. A valid read is
`EF 40 17`; any other non-erased result is reported but does not stop normal
internal-font rendering.

### RIF Parser

`rif_reader` is pure shared logic, copied byte-for-byte between `firmware/src`
and `KEITHLEY_2000_LCD/Core/{Src,Inc}`. It parses the fixed 64-byte `K2RF`
header and fixed 48-byte directory records without holding a complete image in
RAM. It accepts only format version 1.0 and rejects invalid header fields,
directory ranges, payload ranges, alignment, or CRC values.

The reader loads the header at `flash_base=0`, then reads directory records on
demand. It resolves primary digit characters and symbols by RIF kind, identity,
dimensions, and RGB565 little-endian tile layout.

### Renderer Integration

The primary-reading renderer attempts external lookup only after JEDEC and RIF
validation succeed. It streams a resolved tile in bounded chunks from U5 to the
currently hidden LT7680 SDRAM page, preserving the existing transpose and page
presentation rules. It does not cache whole 16 KiB tiles in STM32 RAM.

If any probe, parser, read, lookup, or tile integrity check fails, the affected
glyph falls back to the current built-in bitmap. The reading remains visible and
the renderer never presents a partially completed external glyph.

## Error Handling

| Condition | Behavior |
| --- | --- |
| JEDEC read fails, times out, or is erased | Disable external path; use internal glyphs |
| RIF magic/version/header/directory invalid | Disable external path; report reason; use internal glyphs |
| Entry absent or has unexpected kind/dimensions | Fall back for that glyph |
| Flash FIFO overflow or read timeout | Abort transaction, deassert SS, fall back |
| Tile CRC mismatch | Do not render the tile; use internal glyph |
| Valid external tile | Render only on the hidden page, then present normally |

## Verification

- Host tests cover header decoding, directory bounds, identity lookup, bad CRC,
  malformed ranges, and fallback decisions.
- The shared parser files are byte-identical across both firmware trees.
- Existing firmware tests, offline build, simulator verification, and CubeMX
  Release build remain clean within 64 KiB Flash and 20 KiB RAM constraints.
- First hardware boot logs the JEDEC ID and RIF status before enabling external
  glyphs.
- Hardware acceptance proceeds in order: JEDEC ID, RIF header, directory entry
  for `8`, diagnostic tile, then a live primary reading. Existing internal-font
  rendering must remain usable when the external path is disabled.

## Decision

Raw FIFO reads are selected over LT7680 GTFNT/CGRAM automatic font mode. The
RIF image is a custom, checksummed RGB565 directory format; it is not a proven
hardware font-ROM format. Parsing it explicitly makes the data contract
testable, preserves the existing renderer, and supplies a safe fallback.
