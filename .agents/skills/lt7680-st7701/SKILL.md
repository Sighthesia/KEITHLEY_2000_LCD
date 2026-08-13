---
name: lt7680-st7701
description: Bring up, init, and debug the LT7680A-R graphics controller (RA8876-compatible) driving an ST7701S-based TFT panel over 18-bit RGB, SPI control interface. Use when initializing LT7680 (PLL/SDRAM/panel timing registers), configuring the ST7701S panel over 9-bit SPI (0x11/0x35/0x3A/0x29), displaying test patterns (color bars), or debugging a black screen where LT7680 and panel share a reset line. Covers the K2000 display board pin map (PA0..PA10) and the black-screen root cause (init-before-reset).
---

# LT7680A-R + ST7701S Display Bring-up

Board-specific reference for the KEITHLEY 2000 TFT display board. STM32F103C8T6
talks to an LT7680A-R (RA8876/LT768x family, QFN-68, 10 MHz crystal) over SPI;
the LT7680 drives an ER-TFT4.58-1 panel (320x960, ST7701S) over 18-bit RGB666.

## Pin map (authoritative: `docs/KEITHLEY2000_2026-08-08.tel`)

LT7680 SPI control bus — **3-wire SPI to LT7680** (do not confuse with the
panel's own 9-bit SPI):

| Signal | MCU pin | LT7680 (U2) pin | Notes |
| --- | --- | --- | --- |
| `LCM_SS` | PA4 | 11 (SCS) | chip select, active low |
| `LCM_SCK` | PA5 | 17 (SCK) | |
| `LCM_SDO` | PA6 | 15 (SDO) | MISO (readback) |
| `LCM_SDI` | PA7 | 16 (SDI) | MOSI |
| `LCM_INT` | PA8 | 19 (INT) | input, interrupt |
| `LCM_RES` | PA3 | via BAT54 (U4/U6) | **shared reset line, drives BOTH LT7680 RST and LCD RES** |

Panel 9-bit SPI (ST7701S command/params, 9-bit frames, bit8 = D/C):

| Signal | MCU pin | Panel X6 pin | Notes |
| --- | --- | --- | --- |
| `LCD_CS` | PA0 | 11 | panel chip select |
| `LCD_SCLK` | PA1 | 10 | |
| `LCD_SDI` | PA2 | 9 | 9-bit SDA (bit8 = D/C) |

RGB666 video: LT7680 `PD2..PD23` → panel X6 pins 16..33 (B2..B7, G2..G7,
R2..R7) via RN3A (22R); `PCLK` U2.49, `PDE` U2.48, `HSYNC` U2.47,
`VHYNC` U2.46. W25Q128 (U5) hangs off LT7680 SPI (U2.20-23), NOT the STM32.

## CRITICAL: shared reset line (black-screen root cause)

`LCM_RES` (PA3) fans out through BAT54 to **both** LT7680 RST and the panel's
RESET. Consequences:

- **Initialize the panel AFTER `lt7680_reset()`, never before.** If you run the
  panel's 9-bit init sequence first and then pulse reset, the reset line wipes
  the panel config you just wrote → black screen. This was the actual bug.
- Boot order (verified build11): `lt7680_reset()` → `lt7680_wait_ready()` →
  **blank `REG[12h]=0x08`** (see boot cosmetics) → `hal_panel_init()` (sleeps
  ~200 ms for the panel's RC release) → `lt7680_gfx_init()` → clear+draw with
  display off → 0x48.
- Reset timing: hold low ≥10 ms, release, wait ≥50 ms before talking to LT7680.

## LT7680 SPI protocol (matches reverse-engineered V16 firmware)

4-byte transactions, each with SCS low during the whole exchange:

| Op | Byte 1 | Byte 2 | Direction |
| --- | --- | --- | --- |
| Write register addr | `0x00` | reg | MOSI |
| Write data | `0x80` | data | MOSI |
| Read data/register | `0xC0` | `0x00` (dummy) | read MISO on 2nd byte |
| Read status | `0x40` | `0x00` (dummy) | read MISO on 2nd byte |

Read-modify-write = write reg addr, then read reg, then write reg addr + write
data with the modified value. Register addressing is one byte per register.

## Init register values (verified = V16 original firmware)

All values below match the original firmware and produce working color bars.

### 1. PLL (REG 05h..0Ah) — FOUT = XI × (N/R) ÷ OD, XI = 10 MHz

OD bits: `00→1, 01→2, 10→3, 11→4`; R = bits[5:1], N = bits[8:0] (N8 in bit0
of C1). C1 = `(OD<<6) | (R<<1) | (N>>8)`.

| Reg | Value | Clock | Result |
| --- | --- | --- | --- |
| `05h` | `0x8A` (OD=10b=3, R=5) + `06h` `0x19` (N=25) | PCLK | 10×(25/5)÷3 = **16.67 MHz** |
| `07h` | `0x8A` + `08h` `0x64` (N=100) | MCLK | 10×(100/5)÷3 = **66.7 MHz** |
| `09h` | `0x8A` + `0Ah` `0x64` (N=100) | CCLK | **66.7 MHz** |

Design rules: `CCLK×2 ≥ MCLK ≥ CCLK`; `CCLK ≥ PCLK×1.5`. Set `REG[00h]`=0x80
to start PLL, then poll busy until clear.

### 2. SDRAM (REG E0h..E4h)

| Reg | Value | Meaning |
| --- | --- | --- |
| `E0h` | `0x29` | SDRAM attribute (4 banks / 4K rows / 512 cols) |
| `E1h` | `0x03` | mode register (CAS 3) |
| `E2h` | `0xE6` | auto refresh interval low |
| `E3h` | `0x01` | auto refresh interval high |
| `E4h` | `0x04` → `0x01` | SDRAM control: write 0x04 (precharge?) then 0x01 (start init), poll bit0 until set |

### 3. Panel timing (REG 14h..1Fh), 320×960, 18-bit

V16-derived: H_BACK=80, H_FRONT=16, H_SYNC=16, V_BACK=10, V_FRONT=12, V_SYNC=3.
Register encoding: H values `(reg+1)×8`, V values `reg+1`.

| Reg | Field | Value |
| --- | --- | --- |
| `14h` | HDWR | `(320/8)-1` = 0x27 |
| `15h` | HDWFTR | 0x00 |
| `16h` | HNDR | `(80/8)-1` = 0x09 |
| `17h` | HNDFTR | 0x00 |
| `18h` | HSTR | `(16/8)-1` = 0x01 |
| `19h` | HPWR | `(16/8)-1` = 0x01 |
| `1Ah`/`1Bh` | VDHR | 960-1 = 0x03BF |
| `1Ch`/`1Dh` | VNDR | 10-1 = 0x09 |
| `1Eh` | VSTR | 12-1 = 0x0B |
| `1Fh` | VPWR | 3-1 = 0x02 |

### 4. Control registers (RMW, preserve untouched bits)

| Reg | RMW | Result | Meaning |
| --- | --- | --- | --- |
| `01h` | set 0x09, clear 0x10 | 0x09 | 18-bit RGB666 output, not sleep |
| `02h` | set 0x40, clear 0x86 | 0x40 | memory config |
| `03h` | write 0x00 | — | GFX mode |
| `10h` | write 0x04 | — | panel config |
| `12h` | set 0x08, clear 0x97 | init value | display control (init) |
| `12h` | set 0x68, clear 0x97 | +0x40 | display ON + color-bar test pattern (bit6 + bit5) |
| `13h` | set 0x00, clear 0xE0 | HS/VS low active, DE high active | signal polarity |

## ST7701S panel init (9-bit SPI, after LT7680 reset)

Panel is 320×960, 18-bit RGB666. Command frames are 9 bits: bit8 = D/C
(0=command, 1=param), bits7..0 = byte. Minimal working tail after page 0:

```
0x11 (Sleep Out)  →  delay ~120 ms
0x35 0x00         (Tearing Effect OFF)
0x3A 0x66         (Interface Pixel Format = RGB666 18-bit)
0x29              (Display ON)
```

`0x3A 0x66` is **mandatory** — without RGB666 the panel will not display the
18-bit stream correctly. The full ST7701S sequence (0xFF page switches,
gamma, porch regs) lives in `hal_board.c` `hal_panel_init()`.

## Black-screen debugging checklist

1. Verify SPI to LT7680 (self-test: write REG[01h], read back, check PA6/MISO).
2. Verify LT7680 video timing on scope: PCLK ≈16.67 MHz, HSYNC/VSYNC present.
3. **Check init order**: panel init MUST come after `lt7680_reset()` (shared reset line).
4. Verify backlight (AP3031).
5. Check panel actually accepts 9-bit commands (X6 DB lines show pixel data).
6. Check logic levels: panel VCC is 2.8 V (X6), MCU drives 3.3 V via RN3A/RN2.

## Acceptance (color-bars milestone, verified)

`LT7680_SPI_SELFTEST=0` → reset → panel init → LT7680 init → show color bars;
UART 115200 prints `STATUS=0x..` / `PASS color-bars enabled`, screen shows
color bars. Build: `cmake --build KEITHLEY_2000_LCD/build/Release --target
KEITHLEY_2000_LCD.elf`; flash via halt-then-verify OpenOCD (see
`openocd-stm32-flash` skill).

## Displaying real image data (windows + memory-write port)

The color-bar test pattern is an **internal generator that bypasses the
window system** — it fills the panel even with every window register at 0.
Real image data from Display RAM requires three windows plus the memory
data port (LT768x DS V4.2 §7.1.3, §10.2):

- **Main image**: `MISA` REG[20h..23h]=0, `MIW` REG[24h..25h]=panel width,
  `MWULX/MWULY` REG[26h..29h]=0.  Default `MIW=0` ⇒ turning the test
  pattern off collapses the picture to a single row of bars.
- **Canvas**: `CVSSA` REG[50h..53h]=0, `CVSIMWTH` REG[54h..55h]=panel
  width.  Ignored only in linear addressing mode.
- **Active window** (host-writable region): `AWUL_X/Y` REG[56h..59h]=0,
  `AW_WTH/HT` REG[5Ah..5Dh]=panel size.
- **Color depth**: `AW_COLOR` REG[5Eh]=0x01 (block X-Y addressing, 16bpp;
  default 0 = 8bpp).
  - Register bit fields (LT768x_DS V4.2 §REG[5Eh] AW_COLOR): bit3 = readback
    position select, bit2 = addressing mode (0=block/X-Y, 1=linear),
    bit[1:0] = canvas color depth & memory R/W width — in Block mode **00b=8bpp,
    01b=16bpp, 1xb=24bpp**. So 0x01 is exactly 16bpp; 0x02 (or 0x03) selects
    24bpp.
  - **Do NOT copy the AP-Note flash demo's `REG_WR('h5E, 'h02)`** — that demo
    shows a 1024x768 picture via DMA and uses 0x02 for *24bpp* data, not 16bpp.
    Our canvas is 16bpp, so keep 0x01. Treat a "change 0x01→0x02" suggestion as
    a misread of the color-depth encoding.
- **Memory write procedure**: set active window → write Graphic R/W cursor
  `CURH` REG[5Fh..60h], `CURV` REG[61h..62h] → **address-write the Memory
  Data R/W Port `MRWDP` REG[04h]** → push 16bpp pixels LSB-first.  Skipping
  the MRWDP address write sends the two data bytes into the last-addressed
  register (CURV) instead of Display RAM — a silent no-op on screen.
- **CRITICAL: each display-RAM data byte is its OWN CS transaction.**
  Mirror Levetop's SPI `SPI_DataWrite`: `CS low → 0x80 → byte → CS high`,
  per byte.  Do NOT hold CS low across a burst of `0x80 lo 0x80 hi ...` —
  on this LT7680A-R silicon every byte after the first 0x80 inside one CS
  window is consumed as *data*, so the repeated 0x80 prefixes land in the
  pixel stream and the picture looks like aliased 8bpp (each byte = one
  independent RGB332 pixel, high byte lost).  This is the MRWDP "8bpp
  symptom" — it is a **frame-structure bug, not a colour-depth register
  problem**.  Diagnosed 2026-08-13: GE fills (bypass the SPI data port)
  always showed correct saturated 16bpp while MRWDP text/bars were wrong
  under every REG[02h]/[5Eh]/[10h] value; per-byte CS toggling fixed it
  (white bar finally white, GREEN=E0E0, BLUE=1F1F on screen).

These are applied in `lt7680_gfx_init()` (`configure_windows()`) and used by
`lt7680_gfx_set_pixel()` / `lt7680_gfx_clear()`; both trees (KEITHLEY and
`firmware/`) must stay in sync.  `lt7680_select_reg()` (bus) selects a
register without writing data so a burst can target MRWDP.

### Framebuffer orientation: the panel TRANSPOSES the framebuffer

The RGB panel scans the 320x960 framebuffer with its axes EXCHANGED and NO
reversal (`fb_x` = pixel row → screen column, `fb_y` = pixel column → screen
row). To render an upright, un-mirrored image, write the **transpose** with a
landscape 960x320 UI coordinate space:

```c
fb_x = uy;   /* no +axis reversed */
fb_y = ux;   /* ui x along the panel's long axis */
```

Derived empirically (build6/7/8): reversing only `fb_x` flips the image
upside down; reversing only `fb_y` mirrors it left-right; both reversed =
180° mirror. Do NOT guess the axes — write a test glyph and read the result
against this table. Center by converting logical position in the 960x320 UI
space (`ux0 = (960-w)/2`, `uy0 = (320-h)/2`) and transposing; the LT7680
timing/registers stay 320x960.

### Readback: prove the canvas, not the panel

`lt7680_gfx_peek_pixel(x,y,&px)` reads a canvas pixel back through MRWDP
(cursor + select REG[04h] + two 0xC0 reads, LSB first). Use it after a clear
to prove SDRAM was really written: all-zero samples ⇒ the fill works and any
visible artifact is display-side (timing/panel); non-zero ⇒ memory was not
cleared at that (x,y). This is how the MRWDP-burst clear was disproved on
hardware in one flash.

**SPI-mode readback is aliased — treat dumps as advisory only.** RAMTEX:
"in SPI bus mode the frame buffer memory can not be read". Observed pattern
(2026-08-13): first byte = `0x80 XOR low-byte`, following bytes repeat the
low byte (GREEN wrote `E0 07`, dump showed `60 E0 E0 E0...`). Only the
recovered low byte (`dump[0] ^ 0x80`) is meaningful; byte order, the high
byte and the pixel count are NOT trustworthy. Judge colour correctness from
the SCREEN, never from a readback dump.

### Clearing the canvas: use the Geometry Engine, not a MRWDP burst

A MRWDP burst of zeros ("one burst fills the whole panel") did **not** clear
Display RAM on hardware — the screen kept the pre-existing SDRAM content
(red/black vertical stripes) behind correctly-drawn pixels. `set_pixel`
works because it re-positions CURH/CURV every call; the burst clear relied on
auto-increment that never covered the canvas. Clear/fill via the GE instead:

- **Filled rectangle = `DCR1` REG[76h] = `0xE0`** (bit7 start, bit6 fill,
  bit[5:4]=10b rectangle). `DCR0` REG[67h] only does line/triangle — ignore
  the DCR0 bit[4:1] "rectangle" field in LT768x DS V4.2 (that field is an
  LT7689 feature; 0xA4/0xE4 on DCR0 are both wrong for LT7680A).
- Sequence: set FG color (`FGCR` D2h `R5<<3`, `FGCG` D3h `G6<<2`, `FGCB`
  D4h `B5<<3`) → `GE_SPT` REG[68h..6Bh]=(x1,y1) → `GE_EPT`
  REG[6Ch..6Fh]=(x2,y2) exclusive, full screen uses (0,0,width,height) →
  write REG[76h]=0xE0 → poll STATUS bit3 (0x08, CORE_BUSY) until clear.
- There is no "draw color select" register at D0h/D1h (D0h=FLDR, D1h=F2FSSR);
  the GE always draws in the foreground color.

### Boot sequence cosmetics

Write `REG[12h]=0x08` (display off, test pattern off) immediately after
`lt7680_reset()`+`wait_ready` and do NOT call `show_color_bars()` during a
normal boot: the internal colour-bar test pattern flashes bars on screen
before the demo blanks the display. With display held off the whole init,
the panel stays black until the final `0x48` after the image is drawn.
Reset-order: reset → blank 0x12=0x08 → panel init → gfx init → clear+draw
(display off) → 0x48.
