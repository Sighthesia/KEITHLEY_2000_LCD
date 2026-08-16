# Research: LT7680A-R Serial Flash (W25Q) Register Protocol for Host Flash Reads

- **Query**: Determine the concrete LT7680A-R/RA8876 register protocol needed for
  STM32-host code to read arbitrary bytes from the attached serial flash (U5), so the
  host can parse/read the custom RIF image. Report exact registers/bitfields/sequence,
  constraints, safe RIF-header test, candidate source files in both mirror trees, and
  all sources.
- **Scope**: mixed (internal driver/skill/docs + external primary datasheets)
- **Date**: 2026-08-16

## TL;DR

The LT7680A-R has a **built-in SPI master** that owns the flash. The host cannot
bit-bang the flash directly (flash pins `FLASH_CS/SCLK/SI/SO` = U2.20-23 are wired
only to LT7680, not to the STM32). The host reads arbitrary flash bytes by driving
that SPI master through five registers:

| Reg | Name | Role |
|---|---|---|
| `B7h` | SFL_CTRL | flash/SF select, text/DMA mode, 24/32-bit addr, read cmd code (only needed for DMA/font auto-read; **not needed for raw host reads**) |
| `B8h` | SPIDR | SPI master Tx/Rx FIFO data port (write = push TX, read = pop RX) |
| `B9h` | SPIMCR2 | SPI master control: SS select, SS_ACTIVE, interrupt enables/masks, CPOL/CPHA |
| `BAh` | SPIMSR | SPI master status: TX/RX FIFO empty/full flags + overflow/idle interrupt flags |
| `BBh` | SPI_DIVSOR | SPI clock period divisor, `FSCK = FCORE / ((D+1)*2)` |

Raw host read of flash bytes = emulate the W25Q "Read Data (03h)" instruction through
the FIFO: activate SS, push `0x03 + 24-bit address + N dummy bytes`, then pop the
received bytes from the Read FIFO (first 4 received bytes are the command/address
turnaround and are discarded; data starts at the 5th byte).

The authoritative sequences come from Levetop `LT768x_DS_V42_ENG.pdf` §15.2 (SPI
Master, incl. Verilog loopback reference) and §19.9 (register bitfields), plus the
Winbond W25Q128JV datasheet for the flash instruction set.

---

## Findings

### 1. Hardware context (who owns the flash)

- Authoritative netlist `docs/KEITHLEY2000_2026-08-08.tel`: `U5` = `W25Q128JVSIQ`
  (16 Mbit = 2 MB), SOIC-8; `FLASH_CS`=U2.20→U5.1(CS#), `FLASH_SCLK`=U2.21→U5.6(CLK),
  `FLASH_SI`=U2.22→U5.5(DI), `FLASH_SO`=U2.23→U5.2(DO).
- LT768x DS V4.2 §4.2.4 (QFN-68 pin table): those are the **SPI master** pins
  `SFCS[1:0]#` (21-20), `SFCLK` (22), `SFDO`/MOSI (23), `SFDI`/MISO (24). The flash
  sits on **SPI-0 = SFCS0#** (`SFL_CTRL` bit7 = 0).
- **Note the naming discrepancy in the task text**: the task says "W25Q64JV", but the
  board's authoritative netlist (TEL) and corrected V20 netlist both say
  `W25Q128JV`. W25Q64JV and W25Q128JV share the same instruction set; only the JEDEC
  device-ID byte differs (`0x4017` vs `0x4018`). Treat the JEDEC probe (below) as the
  ground truth.
- The STM32↔LT7680 host bus is the existing 3-wire SPI (PA4 CS, PA5 SCK, PA7 SDI,
  PA6 SDO); all flash access is done by reading/writing LT7680 registers B7h-BBh over
  that bus. No extra MCU pin is involved.

### 2. Register map and bitfields (LT768x DS V4.2 §19.9; identical map exists on RA8876)

`REG[B6h]` **DMA_CTRL** — Serial Flash DMA controller
- bit0 write = DMA start (self-clears); bit0 read = busy (0 idle / 1 busy).
- Only used for flash→Display-RAM DMA (not for host byte reads).

`REG[B7h]` **SFL_CTRL** — Serial Flash/ROM Controller
- bit7 = SF select: 0 = Serial Flash/ROM 0 I/F (SFCS0#), 1 = SF1.
- bit6 = access mode: 0 = Text Mode (CGRAM), 1 = DMA Mode (CGRAM/Pattern/Boot/OSD).
- bit5 = address mode: 0 = 24-bit, 1 = 32-bit (32-bit requires host to send EN4B
  `B7h` to the flash first, then set this bit).
- bit4 = NA.
- bit[3:0] = read command code & behavior (used by the engine's automatic
  read — DMA/font; NOT the raw host path):
  - `000xb`: 1x read `03h`, normal speed, data from SFDI, **no dummy**.
  - `010xb`: 1x read `0Bh` fast read, 8 dummy cycles inserted between address & data.
  - `1x0xb`: 1x read `1Bh` fastest, 16 dummy cycles.
  - `xx10b`: 2x read `3Bh` dual mode 0, 8 dummy.
  - `xx11b`: 2x read `BBh` dual mode 1, 4 dummy.
- Defaults `0x00` (SF0, text mode, 24-bit addr, 03h) are fine for raw host reads.

`REG[B8h]` **SPIDR** — SPI Master Tx/Rx FIFO Data Register
- Write: pushes one byte into the 16-entry **Write FIFO**.
- Read: pops one byte from the 16-entry **Read FIFO**.
- Transmission starts automatically when SS_ACTIVE=1 and the Write FIFO is not empty;
  **each transmitted byte simultaneously receives one byte** into the Read FIFO. To
  read a byte you must write a dummy byte to the Write FIFO.

`REG[B9h]` **SPIMCR2** — SPI Master Control Register
- bit7 = NA.
- bit6 = SPI master interrupt enable (0 = disabled — host still sees flags in
  SPIMSR; 1 = enabled, asserts INT#/process-complete path).
- bit5 = SS# drive select: 0 = SFCS0#, 1 = SFCS1#.
- bit4 = **SS_ACTIVE**: 0 = inactive (SS# high; FIFOs cleared, engine idle), 1 =
  active (SS# low).
- bit3 = OVFIRQMSK: 0 = unmask overflow IRQ, 1 = mask (default 1).
- bit2 = EMTIRQMSK: 0 = unmask "TX empty & engine idle" IRQ, 1 = mask (default 1).
- bit[1:0] = SPI operation mode (CPOL:CPHA): 0=00, 1=01, 2=10, 3=11. When DMA or
  external CGROM is enabled only modes 0 and 3 are supported. Do not change
  CPOL/CPHA while SS_ACTIVE is set.

`REG[BAh]` **SPIMSR** — SPI Master Status Register (all RO except flags)
- bit7 = Tx FIFO Empty flag (1 = empty).
- bit6 = Tx FIFO Full flag (1 = full).
- bit5 = Rx FIFO Empty flag (1 = empty).
- bit4 = Rx FIFO Full flag (1 = full).
- bit3 = Overflow interrupt flag — write 1 to clear.
- bit2 = "Tx FIFO empty & SPI engine/FSM idle" interrupt flag — write 1 to clear.
- bit[1:0] = NA.
- Loopback reference polls for `0x84` (= bit7 Tx-empty + bit2 idle flag) after TX,
  then clears the flag by writing `0x04`.

`REG[BBh]` **SPI_DIVSOR** — SPI clock period
- `FSCK = FCORE / ((Divisor + 1) * 2)`, default 3. With FCORE = CCLK = 66.7 MHz
  (the values this firmware programs), default gives ~8.3 MHz; divisor 0 gives
  ~33 MHz, which is still under the W25Q128JV 50 MHz (03h) / 133 MHz (0Bh) limits.

### 3. Reference sequence (raw SPI-master byte read) — from DS §15.2 loopback code

Verilog loopback reference (`REG_WR` = write reg, `REG_RD` = read reg; SPI mode 3 in
this sample, SFDO looped to SFDI):

```
REG_WR(BBh, 0x1F);                    // divisor -> SPI clock
REG_WR(B9h, 0x1F);                    // {0, mask, SS#_sel, ss_active, ovfirqen,
                                      //  emtirqen, cpol, cpha}  => SS# low (active)
REG_WR(B8h, 0x55); REG_WR(B8h, 0xaa); // TX bytes
REG_WR(B8h, 0x87); REG_WR(B8h, 0x78);
wait(INT#);
REG_RD(BAh); while (acc != 0x84) ...  // wait Tx FIFO empty + engine idle
REG_WR(BAh, 0x04);                    // clear idle interrupt flag
REG_RD(B8h); ... (4x)                 // RX bytes from Read FIFO
REG_WR(B9h, 0x0F);                    // SS# high (deactivate) -> FIFOs reset
```

Adapted to a W25Q "Read Data (03h)" transaction, with SPI **mode 0** (CPOL=0,
CPHA=0) and polled status (mask the two IRQs):

```
1. REG_WR(BBh, DIV)                     // e.g. 0x0F -> ~2 MHz for first bring-up
2. REG_WR(B9h, 0x1C)                    // 0b0001_1100: SS_ACTIVE=1 (SS# low),
                                        //   SFCS0, masks=1, mode 0. (0x50 variant
                                        //   enables INT# instead of polling)
3. push to SPIDR: 0x03, A[23:16], A[15:8], A[7:0]   // W25Q read-data instruction
4. push to SPIDR: N dummy bytes         // each dummy clocks one received data byte
5. drain in chunks: every ≤16 SPIDR writes, poll SPIMSR and read SPIDR N times;
   first 4 received bytes are turnaround (discard), bytes 5..4+N are the data
6. REG_WR(B9h, 0x0C)                    // SS_ACTIVE=0 (SS# high) -> FIFOs reset
```

### 4. Flash instruction-level protocol (Winbond W25Q128JV / W25Q64JV)

- **Read Data `03h`**: `CS# low → 0x03 → A23-A16 → A15-A8 → A7-A0 → data bytes →
  CS# high`. No dummy cycles. Data starts shifting out on the clock right after the
  last address bit — i.e. the first valid data byte is the **5th** received byte of
  the transaction (bytes 1-4 are command+address turnaround).
- **Fast Read `0Bh`**: same + 1 dummy byte (only relevant if SFL_CTRL is used for
  DMA; for raw host reads you would push the dummy yourself).
- **JEDEC ID `9Fh`**: `0x9F` then 3 ID bytes (no address). W25Q128JV → `EF 40 18`;
  W25Q64JV → `EF 40 17`. A 4-byte transaction is the ideal first hardware probe
  (no chunking needed).
- The flash is 3.3 V SOIC-8 on the LT7680 SPI master; SFCS0# is the chip select the
  raw path must use (`SPIMCR2` bit5 = 0).

### 5. Constraints / busy / error semantics

- **Read FIFO depth = 16**: DS §15.2 "before receiving every 16 bytes of data, the
  host must confirm if Read-FIFO is empty or not". For >12 data bytes per
  transaction (4 cmd/addr + 12 data = 16), drain the Read FIFO in ≤16-transfer
  batches. A 64-byte RIF header read = 4+64=68 transfers: e.g. 4×16 + 4, draining 16
  after each full batch.
- **Write FIFO overrun**: writing when full overwrites oldest bytes → wrong data
  transmitted; unrecoverable until SS_ACTIVE is cleared (which resets both FIFOs).
  Pace SPIDR writes by checking SPIMSR bit6 (Tx full).
- **Read FIFO overrun**: if not drained, older bytes are overwritten; the DS
  recommends dummy reads to re-align: `Ndummy_reads = Ntransmitted_bytes mod 16`.
- **SS_ACTIVE is the reset**: clearing bit4 of SPIMCR2 raises SS# and clears both
  FIFOs; the engine idles. Always end a transaction by clearing it.
- **Busy/error polling**: no dedicated SPI-master busy bit in the global status
  register; use SPIMSR bit7/bit2 (Tx empty & idle) to know the transaction finished,
  and bit3 (overflow flag) for errors. The global "process complete" interrupt
  (REG[0Ch] bit2) is the DMA-complete signal; DS note: if that flag is not set after
  an interrupt, check SPIMSR (BAh) directly.
- **Host-side SPI readback of registers works**: `lt7680_read_reg()` (0xC0 opcode,
  `lt7680_bus.c:115-136`) is used throughout init (REG[01h] self-test, REG[12h]
  read-back, REG[E4h] poll) and works on this board. SPIDR is a plain register, so
  reading it through the same 0xC0 path is expected to work; the known "aliased
  readback" bug is specific to the **MRWDP display-RAM data port** (skill §Readback,
  `lt7680_gfx_peek_pixel`), not to register reads. Still, verify a known constant
  (JEDEC ID) before trusting bulk flash reads on this silicon.

### 6. Safe RIF-header test (lowest-risk hardware bring-up)

1. **JEDEC probe** (proves the whole SPI-master path, 1 transaction, no chunking):
   SS active (mode 0) → push `0x9F, 0x00, 0x00, 0x00` → wait idle → pop 4 bytes →
   expect `[0xFF, 0xEF, 0x40, 0x18]` (W25Q128JV; `0x17` for W25Q64JV) → SS inactive.
2. **RIF header read** at the image's recorded `flash_base` (default `0x000000`):
   read 64 bytes, then check per `tools/rif_common.py` header layout
   (`<4sHHIIIIHHIIIB23s`):
   - bytes 0-3 == `"K2RF"` (0x4B 0x32 0x52 0x46)
   - bytes 4-5 == 0x01, bytes 6-7 == 0x00 (version 1.0)
   - optionally bytes 8-11 `image_size` ≤ flash size, and header CRC (bytes 12-15)
     vs zlib CRC32 over the 64 header bytes with both CRC fields zeroed.
   - If the flash still holds the original V15/V16 resource data (not yet RIF), the
     magic check fails with `0xFF` or original data — do not treat that as a driver
     failure; it means the image was never programmed.
3. Read a known payload tile (e.g. digit `8` at `0x001000` from the default map) and
   compare to `tools/verify_resource_flash.py` expectations if deeper validation is
   wanted.

### 7. Candidate files/functions to change (both mirror trees)

Drivers may differ between trees per AGENTS.md, but the public API should be added
identically:

- `KEITHLEY_2000_LCD/Core/Inc/lt7680_gfx.h` **and** `firmware/src/lt7680_gfx.h`:
  declare e.g. `lt7680_status_t lt7680_flash_read(uint32_t addr, uint8_t *buf,
  uint32_t len)` (plus optional `lt7680_flash_read_jedec_id` / presence probe).
- `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c` **and** `firmware/src/lt7680_gfx.c`:
  add register defines (`LT7680_REG_SFL_CTRL 0xB7`, `SPIDR 0xB8`, `SPIMCR2 0xB9`,
  `SPIMSR 0xBA`, `SPI_DIVSOR 0xBB`), the FIFO push/pop + chunked drain logic, and
  the flash-read function. These two files currently differ (firmware/src is an older
  skeleton with `REG_*` naming), so the change must be ported to both.
- `lt7680_bus.h` / `lt7680_bus.c` (both trees): no new primitives strictly required —
  existing `lt7680_write_reg`/`lt7680_read_reg` cover B7h-BBh. Optional: a
  `lt7680_write_reg_burst` if pushing TX bytes must be faster than one 4-byte CS
  transaction per byte (note: unlike MRWDP pixel writes, SPIDR writes are normal
  register writes — each is its own CS transaction, which is fine).
- `KEITHLEY_2000_LCD/Core/Src/main.c`: optional boot-time JEDEC probe + RIF header
  sanity print over UART (diagnostic only).
- New pure-logic RIF parser (mirrored): `firmware/src/rif_reader.c/h` +
  `KEITHLEY_2000_LCD/Core/{Src,Inc}/` byte-identical, mirroring
  `tools/rif_common.py` header/directory parsing; add to the `SRCS` list in
  `firmware/tests/run_tests.sh` (manual maintenance required per AGENTS.md) and add
  host tests in `firmware/tests/`.
- `firmware/tests/run_tests.sh`: SRCS already lists `src/lt7680_gfx.c`,
  `src/lt7680_bus.c` (line 8) — extend with any new mirrored module.

## Caveats / Not Found

- **W25Q64JV vs W25Q128JV**: task text says W25Q64JV; the authoritative netlist
  (TEL + corrected V20) says **W25Q128JV** (U5). Both are Winbond SPI NOR with the
  same commands; only JEDEC ID differs. Confirm on hardware via `9Fh` probe.
- **No public reference implementation found** of raw SPI-master flash reads through
  RA8876/LT7680 — only font-ROM *configuration* exists (xlatb/ra8876
  `initExternalFontRom`, danmeuk/esp_lcd_ra8876 headers). The DS §15.2 loopback
  snippet is the only authoritative working sequence.
- **SPIDR readback over the 3-wire host SPI is not yet verified on this board**.
  The MRWDP aliasing lesson does not obviously apply (SPIDR is a register, not the
  display-RAM data port), but first validate with the JEDEC ID.
- **Power-on Display boot loader**: if the flash contains a boot-start image the
  LT7680's power-on display may have already accessed the flash at boot; the raw SPI
  master path is independent and safe to use after the host init completes, but if
  reads misbehave, check whether `SFL_CTRL` was left in DMA mode by a prior stage.
- **Timing**: divisor formula assumes FCORE = CCLK (66.7 MHz as programmed); if the
  host changes PLL, recompute. First bring-up should use a conservative divisor.
- **DMA is not suitable for RIF parsing**: DMA (B6h + BCh-BFh + C0h-C5h) streams
  flash→Display RAM in display-pixel format; the RIF header/directory must be read
  as raw bytes by the host via the SPI-master FIFO path described above.

## Sources

### External (primary)
- Levetop `LT768x_DS_V42_ENG.pdf` (LT768x datasheet V4.2) —
  https://www.levetop.cn/uploadfiles/2023/05/LT768x_DS_V42_ENG.pdf
  — §4.2.4 pin table (SFCS[1:0]#/SFCLK/SFDO/SFDI), §15.2 SPI Master (FIFO semantics,
    overrun, loopback reference), §15.3/15.3.1-15.3.3 Serial Flash Controller +
    read-command table (Table 15-2), §19.9 Serial Flash & SPI Master Control
    Registers (B6h-BBh bitfields).
- Levetop `LT768x_AP-Note_V12_ENG.pdf` —
  https://www.levetop.cn/uploadfiles/2023/05/LT768x_AP-Note_V12_ENG.pdf
  — §15/15.1 DMA transfer of SPI flash (W25Q128 24-bit example), §15.5 how a
    program calls flash bin files. (DMA only; confirms flash is driven through the
    LT7680 SPI master.)
- Winbond `W25Q128JV` datasheet (e.g.
  https://pdf.ample-chip.com/442708/W25Q128JVFIM.pdf and
  https://www.allelcoelec.pt/datasheets.b1/W25Q128JWSIM.pdf) — instruction set:
  Read Data `03h` (no dummy), Fast Read `0Bh` (+1 dummy), JEDEC ID `9Fh`
  (W25Q128 = EF 40 18; W25Q64 = EF 40 17), SPI modes 0/3.
- RAiO `RA8876_77_AP_User_Guide_v0.2_EN.pdf` —
  https://www.raio.com.tw/data_raio/RA887677/AP/RA8876_77_AP_User_Guide_v0.2_EN.pdf
  — confirms the same SPI-master/dma architecture on the RA8876 family.

### External (reference drivers — config only, no raw-read example)
- https://github.com/xlatb/ra8876 (`src/RA8876.h` §19.9 constants, §19.2 SPI host
  opcodes 0x00/0x80/0xC0, `initExternalFontRom` in `src/RA8876.cpp`) —
  SFL_CTRL=0xB7, SPI_DIVSOR=0xBB, GTFNT_SEL=0xCE.
- https://github.com/danmeuk/esp_lcd_ra8876 (`ra8876_registers.h`) — same
  B6h-BFh register set (DMA_CTRL 0xB6, SFL_CTRL 0xB7, SPIDR 0xB8, SPIMCR2 0xB9,
  SPIMSR 0xBA, SPI_DIVSOR 0xBB, DMA_SSTR 0xBC-0xBF).

### Local
- `docs/KEITHLEY2000_2026-08-08.tel` — U5 = W25Q128JVSIQ; `FLASH_CS`=U2.20→U5.1,
  `FLASH_SCLK`=U2.21→U5.6, `FLASH_SI`=U2.22→U5.5, `FLASH_SO`=U2.23→U5.2.
- `docs/Netlist_K2000_Display Board_TFT V20.txt` (2026-08-08 corrected) — same U5
  flash wiring, "W25Q128（U5）挂 LT7680 SPI（U2.20-23），不经 STM32".
- `KEITHLEY_2000_LCD/Core/Src/lt7680_bus.c` / `.h` — host SPI opcodes, existing
  `lt7680_read_reg` (0xC0) / `lt7680_write_reg` / `lt7680_select_reg` / status poll.
- `KEITHLEY_2000_LCD/Core/Src/lt7680_gfx.c` / `firmware/src/lt7680_gfx.c` —
  driver register map and where flash registers/functions would be added.
- `.agents/skills/lt7680-st7701/SKILL.md` — board context, host SPI protocol, MRWDP
  readback aliasing caveat (display RAM only), PLL values (CCLK 66.7 MHz).
- `tools/rif_common.py` + `tools/README.md` + `tools/verify_resource_flash.py` —
  RIF header/directory layout, magic `K2RF`, version 1.0, default flash map
  (header at 0x000000, digit tiles from 0x001000).