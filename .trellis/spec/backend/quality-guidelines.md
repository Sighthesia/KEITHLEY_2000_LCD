# Embedded Runtime Quality Guidelines

## Scenario: Interrupt RX, Trend Projection, and Cooperative Display

### 1. Scope / Trigger

Apply this contract when firmware receives protocol bytes in an ISR while the
main loop performs LT7680 drawing, or when readings are retained in a rolling
trend buffer. It prevents byte loss during slow graphics operations, stale
ring slots reappearing after wrap, and large automatic objects exhausting the
STM32F103C8 1KB reserved stack.

### 2. Signatures

```c
#define UART_RX_QUEUE_CAPACITY 512u
void uart_rx_queue_push_isr(uart_rx_queue_t *queue, uint8_t byte);
int uart_rx_queue_pop(uart_rx_queue_t *queue);
uint32_t uart_rx_queue_overflow_count(const uart_rx_queue_t *queue);

#define TREND_BUCKET_MS 20u
#define TREND_BUCKET_COUNT 500u
#define TREND_MAX_COLUMNS 240u
bool trend_buffer_add(trend_buffer_t *trend, uint32_t now_ms,
                      const char *text, const char *unit);
uint16_t trend_buffer_project(const trend_buffer_t *trend, uint32_t now_ms,
                              trend_column_t *columns, uint16_t capacity);
```

`hal_uart_rx_irq()` may capture bytes and error status only. Protocol parsing,
model mutation, trend conversion, and drawing belong to the main loop.

### 3. Contracts

- UART queue indices are monotonic 16-bit counters. Mask only when addressing
  the 512-byte array; `head - tail == 512` means full capacity is occupied.
- ISR reads USART status then data to clear RXNE/ORE/NE/FE in STM32-defined
  order. The main loop briefly masks USART1 IRQ when atomically changing queue
  indices; it does not globally disable SysTick or unrelated interrupts.
- Overflow increments a counter, discards queued/arriving bytes, and publishes
  the first subsequent `0x0D` as the parser resynchronization byte.
- Trend buckets store min/max in normalized base units and include occupancy
  plus enough generation/time information to reject wrapped slots outside the
  current 10-second window.
- Decimal parsing is bounded, locale-independent, rejects overflow/Inf, and
  does not use heap allocation, `strtof`, or formatted I/O.
- Trend history and projection arrays are static or caller-owned. Never place
  500 buckets or 240 columns in an automatic target stack frame.
- Rendering is resumable. Each step emits bounded bitmap runs or graph columns,
  then returns so the main loop can drain RX. The target acceptance limit is a
  measured worst-case slice below 10ms.
- LT7680 double buffering uses two SDRAM pages. Render only to the non-visible
  `CVSSA` page and present it only after completion by changing `MISA`; use a
  1MiB-aligned second page so multi-register `MISA` writes do not expose an
  intermediate display address.
- Build each page's static base independently during its first hidden render.
  Do not copy a complete 320x960 RGB565 page on every update: at 614400 bytes,
  that transfer dominates the frame interval. Before presenting an existing
  page, repaint all dynamic regions that can differ from the currently visible
  page and track each page's completed text/trend generation separately.
- Trend raster caches are page-local. With unchanged trend limits, compare the
  new projection against that page's cached columns and repaint only changed
  columns. A change in data availability or axis limits requires clearing and
  rebuilding the full trend region, including grid and labels.
- Do not use partial BTE page synchronization for alternate-frame presentation
  on this LT7680A-R board. A correctly transposed trend-band transfer
  (`x=192`, `y=0`, `w=128`, `h=960`) still caused physical left-side black
  flashes. Use the verified full `320x960` page copy before `MISA` present
  until a controller-level partial-copy acceptance test proves otherwise.
- For BTE memory-copy-with-ROP of RGB565 pages, `BTE_CTRL1` must be `0xC2`
  (ROP code 12 = copy S0, operation 2 = memory copy) and `BTE_COLR` must be
  `0x25` (S0/S1/destination 16bpp). `0xF2` selects ROP whiteness and produces
  white/inverted regions; never treat the ROP nibble as a bus-width field.
- A complete page copy plus all 240 trend columns still bounds presentation
  rate even when every individual slice is cooperative. Measure the full
  copy-to-present interval on hardware before claiming the 10Hz/5Hz ceilings.
- At boot, clear both SDRAM pages before enabling display output. Present the
  cleared page 0 first, keep it visible while the initial frame is built on
  hidden page 1, and only present page 1 after the cooperative renderer reports
  completion. This guarantees a visible black screen even if first-frame
  rendering is slow or stalls.
- The initial reading phase must render only the primary value/unit (or its
  empty-state message) before advancing to the initial trend background, axes,
  and columns. Defer the right-side metadata cells to `UPDATE_READING`; their
  many small bitmap transactions must not delay first trend visibility.
- After every successful `MISA` page presentation, re-apply `REG[12h]=0x48`
  before continuing normal rendering. The LT7680 panel can remain blank after
  a hidden-page latch unless the display-enable value is written again.
- Do not use `lt7680_gfx_copy_page()` for runtime alternate-page refresh on the
  K2000 board. Even the full-page BTE copy can blank the panel; select the
  hidden page, clear it with GE, and rebuild all required regions before MISA.
- After the first page is presented, runtime refreshes must keep `CVSSA` and
  `MISA` on the visible page and repaint only dirty regions in place. The
  hidden-page path is reserved for initial construction; live page switching
  causes a full-screen blank interval on this board.
- The initial hidden page may be presented exactly once. Runtime renderer
  completion must not call `MISA` again, even when the active page is unchanged;
  same-page presentation also causes a full-screen refresh on this controller.
- During LT7680 bring-up, retain the completed initial frame and gate all later
  renderer work until runtime GE refresh has a separate hardware acceptance
  path. This isolates first-frame stability from live-refresh failures.
- During LT7680 bring-up, retain the completed initial frame and gate all later
  renderer work until runtime GE refresh has a separate hardware acceptance
  path. This isolates first-frame stability from live-refresh failures.
- The initial hidden page may be presented exactly once. Runtime renderer
  completion must not call `MISA` again, even when the active page is unchanged;
  same-page presentation also causes a full-screen refresh on this controller.

### Boot-Time RIF Safety

Do not run `rif_init()` before the first visible display frame. The external
Flash probe changes LT7680 serial-Flash state that is not yet validated to
coexist with graphics operations. Keep `s_rif_ready=false`, preserve the
reset -> panel-init -> gfx-init -> clear -> present order, and render with the
built-in `font_text` fallback. Re-enable RIF only behind a separately
validated runtime path that restores the LT7680 graphics state.

### 4. Validation & Error Matrix

| Condition | Required behavior |
| --- | --- |
| Queue contains 512 bytes | Accept all 512; overflow only on byte 513 |
| UART ORE/NE/FE | Read SR then DR, count/drop affected input, keep ISR bounded |
| Queue overflow | Enter recovery, ignore bytes until `0x0D`, emit that delimiter |
| Numeric exponent/value exceeds `FLT_MAX` | Reject sample; never store Inf/NaN |
| Tick moves backward or cannot map safely | Reset trend history |
| Ring slot generation is outside visible window | Treat slot as unoccupied |
| Measurement dimension changes | Clear history before adding the new sample |
| `OVERFLOW`, `----`, malformed number/unit | Do not insert trend sample |
| Render frame changes mid-paint | Finish or explicitly supersede at a safe state boundary |
| BTE page copy uses CTRL1 `0xF2` | Reject it: white/inverted copied regions mean ROP whiteness was selected |
| Double-buffer page switch | Do not present until BTE and all dirty-region GE work are idle |
| Alternate page has an older text/trend generation | Repaint that page's dynamic region before MISA present; never present it as-is |

### 5. Good/Base/Bad Cases

- Good: 500 readings/s collapse into 50Hz min/max buckets and preserve a short
  spike in the 240-column projection.
- Base: no samples leaves an empty projection and visible static grid.
- Bad: masked head/tail indices use one empty slot and silently reduce a
  documented 512-byte queue to 511 bytes.
- Bad: projection checks only array index and lets an old wrapped slot reappear
  as a future sample.
- Bad: treating an apparently fast BTE page copy as proof of 10Hz refresh while
  still redrawing every trend column and large-glyph stroke before present.
- Bad: sharing one curve cache between alternate SDRAM pages, causing the
  renderer to skip columns that exist only on the other page.

### 6. Tests Required

- Push exactly 512 UART bytes, assert no overflow, push one more, assert one
  overflow and recovery mode; then assert `0x0D` is the first recovered byte.
- Assert FIFO ordering and empty return `-1`.
- Reject malformed numbers, invalid UTF-8 unit prefixes, huge mantissas, and
  huge exponents.
- Assert same-bucket min/max preservation, dimension reset, timeout reset,
  backward-tick reset, wrap generation filtering, and 500-to-240 projection.
- Build target with `-Werror`, run `arm-none-eabi-size`, and inspect that trend
  arrays reside in static RAM rather than an automatic stack frame.
- Measure cooperative renderer slices on hardware; host tests can verify work
  budgets/state progression but cannot prove elapsed microcontroller time.
- Verify each independently initialized page contains the static base and its
  current dynamic regions before MISA presentation. Alternate pages under a
  changing reading and confirm no older text or curve returns.

### 7. Wrong vs Correct

#### Wrong

```c
next = (head + 1u) & 511u;
if (next == tail) overflow(); /* capacity is only 511 */

void USART1_IRQHandler(void) {
    k2000_proto_feed((uint8_t)USART1->DR); /* parsing inside ISR */
}
```

#### Correct

```c
if ((uint16_t)(head - tail) >= UART_RX_QUEUE_CAPACITY) {
    enter_recovery();
} else {
    data[head & (UART_RX_QUEUE_CAPACITY - 1u)] = byte;
    head++;
}

void USART1_IRQHandler(void) {
    hal_uart_rx_irq(); /* status/data capture only */
}
```

## Review Checklist

- [ ] Shared pure-logic files are byte-identical between `firmware/src` and
      `KEITHLEY_2000_LCD/Core/{Src,Inc}`.
- [ ] ISR contains no parser, trend, display, delay, or blocking operation.
- [ ] Renderer returns to RX draining between bounded steps.
- [ ] Flash remains within 64KB and RAM within 20KB, including heap/stack
      reservations.

## Scenario: RIF External Font Runtime

### 1. Scope / Trigger

Apply this contract when generating or reading the K2RF resource image through
the LT7680A-R serial-flash master. The netlist names W25Q128JV, but the
installed and verified device is W25Q64JV (`EF 40 17`, 8 MiB); runtime bounds
must use the detected device capacity. U5 is not on the STM32 SPI bus.

### 2. Signatures

```sh
python3 tools/pack_resource_flash.py \
  --src-dir firmware/src --digit-src-dir tools/font_source --output resources.img \
  [--base-offset 0x000000] [--fg '#00FF33'] [--bg '#000000']

python3 tools/verify_resource_flash.py resources.img \
  [--src-dir firmware/src] [--digit-src-dir tools/font_source]

lt7680_status_t lt7680_flash_read(uint32_t address, uint8_t *data,
                                   uint16_t length);
lt7680_status_t lt7680_flash_read_jedec_id(uint8_t id[3]);
void lt7680_flash_get_b7_probe(lt7680_flash_b7_probe_t *probe);
void lt7680_flash_get_fifo_probe(lt7680_flash_fifo_probe_t *probe);
void lt7680_flash_get_jedec_probe(lt7680_flash_jedec_probe_t *probe);
void lt7680_flash_get_header_probe(lt7680_flash_header_probe_t *probe);
rif_status_t rif_reader_parse_header(const uint8_t *data, uint16_t len,
                                     rif_image_t *out);
rif_status_t rif_reader_parse_entry(const rif_image_t *image,
                                    const uint8_t *data, uint16_t len,
                                    rif_entry_t *out);
rif_status_t rif_reader_find_glyph(const rif_image_t *image,
                                   const rif_entry_t *entry, uint32_t kind,
                                   uint16_t code, rif_tile_t *out);
```

The packer emits a deterministic `K2RF` image. The default image is
`0xBE000` bytes, 4 KiB sector-aligned, and records its absolute
`flash_base` in the header. A full 8 MiB dump is also accepted by the
verifier; it validates the image slice at the recorded base.

### 3. Contracts

- The physical W25Q64 capacity is 8 MiB; `base-offset` must be 4 KiB aligned
  and `base-offset + image_size <= 0x800000`.
- The image contains a 64-byte header, 44 directory entries, RGB565
  little-endian glyph tiles, a diagnostic tile, and `0xFF`-filled reserved
  `font_text` and `ui_assets` regions.
- Payload entries are 4 KiB aligned and carry dimensions, character identity,
  colors, size, and CRC32. Archived 64x128 digit bitmaps live in
  `tools/font_source`; active small fonts remain in `firmware/src`.
- `lt7680_flash_read()` uses W25Q command `0x03` with a 24-bit address through
  LT7680 B8/B9/BA/BB. It activates CS with B9=`0x1F` (SFCS0#, mode 3), transfers no more than
  16 FIFO bytes per batch, waits for SPIMSR `TX_EMPTY` before draining RX, and
  writes B9=`0x0C` on every exit. Do not require SPIMSR `IDLE`: it is
  interrupt-mask dependent on this controller and timed out on hardware.
- Before a raw FIFO transaction, write SFL_CTRL B7=`0x00` and immediately read
  it back, then enable CCR SPI-master bit 1. Some LT7680 revisions may gate
  B7 writes after host SPI-master mode is enabled. B7 restores the
  SF0/text/24-bit raw default if the boot loader left automatic-font or DMA
  configuration behind. Do not write
  `0x14`: that selects the automatic-font FAST_READ configuration and sets a
  reserved bit; raw reads send `0x03` or `0x9F` through B8 directly.
- Firmware first probes the RIF header with `0x03` and uses that parsed header
  as the readiness gate. The `0x9F` JEDEC probe is diagnostic-only and runs
  after the header transaction has completed; a JEDEC failure must not block a
  valid header from enabling RIF readiness. It must read and parse the RIF
  header, then scan directory entries before using a glyph. It streams no more than 64 tile
  bytes at once, renders only non-background RGB565 runs on the hidden page,
  and falls back to `font_text` when any probe/read/format step fails.
- `rif_init()` emits one read-only snapshot of LT7680 `B7/B9/BA/BB` plus `01`
  (the host interface/CCR) both before JEDEC probing (`regs`) and immediately
  after `lt7680_flash_read_jedec_id()` returns (`post`), including when the
  result is `FF FF FF` or an error. The snapshots must use
  `lt7680_read_reg()` only, must not read `B8/SPIDR`, write status-clear bits,
  or alter the raw FIFO transaction. `B7=00`, an enabled SPI-master bit in
  `01`, `B9=1F` (or the selected documented mode), and a non-error `BA`
  distinguish controller setup from an external no-response; the snapshots
  themselves never prove U5 connectivity.
- The B7 write probe is initialized as "not attempted" with both operation
  statuses set to `LT7680_ERR_BUS`. Set `attempted` before calling the B7 write,
  then record the write result and immediate B7 readback result independently.
  This distinguishes an uncalled probe, a failed write, an unreadable register,
  and a successful immediate `0x00` readback that is later overwritten.
- The FIFO probe (`lt7680_flash_fifo_probe_t`) records TX_FULL observations
  before each SPIDR write during flash FIFO transactions. It is zero-initialized
  (attempted=0, full_count=0, status_err=0, last_status=0). Before each SPIDR
  push, read SPIMSR and increment `full_count` if bit6 (TX_FULL) is set; if the
  SPIMSR read fails, set `status_err=1` but continue the existing raw operation.
  The probe never alters transmitted bytes, batch sizes, B9 values, SPI mode,
  commands, or cleanup. `lt7680_flash_get_fifo_probe()` returns a snapshot; the
  CubeMX main prints it as `RIF FIFO probe attempted=XX full=XX status_err=XX
  last_status=0xXX` after the JEDEC line.
- The JEDEC probe (`lt7680_flash_jedec_probe_t`) captures the raw 4-byte SPIDR
  read from a JEDEC ID transaction (0x9F). It is zero-initialized
  (attempted=0, raw[0..3]=0, status=LT7680_ERR_BUS). Set `attempted` before
  calling `lt7680_flash_read_jedec_id()` after the header readiness probe, then
  record the transaction status and the four raw bytes read from the SPI FIFO:
  `raw[0]` is the turnaround byte (discarded by the normal API), `raw[1..3]`
  are the three manufacturer/device ID bytes. The probe never alters
  transmitted bytes, batch sizes, B9 values, SPI mode, commands, or cleanup.
  `lt7680_flash_get_jedec_probe()` returns a snapshot; the CubeMX main prints
  it as
  `RIF JEDEC raw=XX XX XX XX` after the JEDEC ID line. This diagnostic
  distinguishes U5 MISO stuck-low (all bytes 0x00) from LT7680 SPIDR readback
  anomalies (all bytes 0xFF or garbled).
- U5 programming is a separate, read-back-verified operation. Do not modify
  bytes outside `[base-offset, base-offset + image_size)`.
- The header probe (`lt7680_flash_header_probe_t`) is populated by the existing
  read of at least 16 bytes from address zero. It records the read status and
  the first 16 returned bytes without issuing another flash transaction. The
  CubeMX diagnostic prints `RIF header status=0xXX attempted=XX raw=...` after
  that read and before header parsing. This is diagnostic-only: it must not
  change the 64-byte read, parser, fallback, FIFO byte sequence, B7/B8/B9
  setup, SPI mode, commands, cleanup, or the U5 read-only boundary. If the
  read fails, the snapshot bytes are cleared rather than treating an
  uninitialized caller buffer as flash data.
- `lt7680_flash_spi_snapshot_t` is a read-only diagnostic for the unresolved
  automatic-serial-Flash/DMA path. It reads B6 (`DMA_CTRL`), B7 (`SFL_CTRL`),
  B9 (`SPIMCR2`), BA (`SPIMSR`), and BB (`SPI_DIV`) through
  `lt7680_read_reg()` and prints `RIF SPI snapshot attempted=XX B6=0xXX
  B7=0xXX B9=0xXX BA=0xXX BB=0xXX status=0xXX`. It must not write B6 or any
  DMA source/destination register, start DMA, access display RAM, change the
  raw FIFO path, or modify the U5. The datasheet evidence is insufficient to
  support an automatic-DMA experiment until a complete reversible sequence is
  known.

### 4. Validation & Error Matrix

| Condition | Required behavior |
| --- | --- |
| Missing source glyph or malformed generated C array | Packer fails before writing output |
| Non-4 KiB base or image outside 8 MiB | Packer and verifier reject it |
| Bad magic/version, header CRC, image CRC, entry CRC, size, overlap, or alignment | Verifier rejects the image |
| Full dump with image at recorded base | Verifier slices and validates the image |
| Full dump with image at another base | Reject unless exported as the matching target range |
| Source glyph round-trip differs | `--src-dir` verification fails |
| JEDEC is not `EF 40 17` or the probe errors | Log diagnostics and continue to the RIF header read |
| FIFO overflows/times out, RIF header read/parse fails | Disable RIF and render the reading with `font_text` |
| JEDEC returns `FF FF FF` after the B7 raw-default reset | Preserve fallback and investigate B9 CS/mode or external Flash wiring; do not change U5 contents |
| Directory entry has wrong kind/code/64x128 geometry | Continue scanning; fall back if no matching glyph exists |
| Tile read or GE run fails | Abort the RIF job, preserve responsiveness, and fall back on its next draw step |
| SPIMSR read fails during FIFO probe | Set status_err=1, continue raw operation, do not abort the transaction |
| Serial-Flash/DMA sequence is incomplete or lacks a known reversible display-RAM destination | Take only the B6/B7/B9/BA/BB read-only snapshot; do not start DMA or write new registers |
| Boot-time RIF probe is not validated against graphics state | Defer `rif_init()`, keep `s_rif_ready=false`, and display the reading with `font_text` |

### 5. Good/Base/Bad Cases

- Good: boot logs `RIF JEDEC=EF4017` and `RIF external digits ready`; the
  hidden page receives bounded RGB565 runs from a matching 64x128 tile.
- Base: an unknown character or unavailable flash renders an upright, visible
  12x24 `font_text` reading instead of blanking the reading area.
- Bad: access U5 from STM32 SPI, send more than 16 FIFO bytes without draining,
  leave LT7680 CS active after an error, or allocate a full 16 KiB tile buffer.

### 6. Tests Required

- Assert deterministic two-pass packing and exact `0xBE000` image size.
- Assert all 44 entries, payload alignment, CRCs, reserved fill, and diagnostic
  color bands.
- Assert source glyph 1bpp to RGB565 to 1bpp round-trip for every tile.
- Verify a complete 8 MiB dump and reject tampered bytes, bad magic, size
  mismatch, misaligned base, and capacity overflow.
- Assert RIF parser header/entry range and glyph-identity rejection cases, plus
  flash-read null, zero-length, and 24-bit-address rejection cases.
- Build the target below 64 KiB Flash and 20 KiB RAM. On hardware, confirm
  JEDEC/header logs, external glyph `8`, a live reading, and the small-font
  fallback path; judge color and orientation from the panel.

### 7. Wrong vs Correct

#### Wrong

```c
uint8_t tile[16384];
lt7680_flash_read(entry.offset, tile, sizeof(tile));
```

This exceeds the bounded-RAM renderer contract and does not guarantee U5 CS
cleanup after a failed transfer.

#### Correct

```c
uint8_t pixels[64];
if (lt7680_flash_read(tile.offset + row_offset, pixels, sizeof(pixels)) != LT7680_OK) {
    disable_rif_and_fall_back();
}
```

The renderer draws each non-background run onto the hidden page, returns to the
main loop between bounded slices, and never writes U5 during normal operation.
