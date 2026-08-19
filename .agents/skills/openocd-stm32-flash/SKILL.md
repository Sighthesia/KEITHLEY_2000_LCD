---
name: openocd-stm32-flash
description: Flash and debug STM32 via OpenOCD with ST-Link over SWD. Use when flashing a binary to an STM32, getting "Unable to reset target", "timed out while waiting for target halted", a `program ... reset` command that fails at the reset step, or a debug-connection that only works with a manual reset button / BOOT0. Covers reset-config choices and the reliable halt-then-flash command form.
---

# OpenOCD + STM32 Flash/Debug

Common failure when flashing STM32 (esp. STM32F103) with ST-Link V2 over
`hla_swd`: the **reset step fails**, not the erase/write. It shows as:

```
Info : [stm32f1x.cpu] halted due to debug-request, ... pc: 0x08000xxx
** Unable to reset target **
```
or, with srst enabled:
```
Error: timed out while waiting for target halted
TARGET: stm32f1x.cpu - Not halted
```

Key insight: these errors mean **nothing was flashed this run** — the traceback/
halt message points at `reset init` (first command) while `pc: 0x0800xxxx` is the
old firmware still running. Never assume the failure is in the flash write.

## Root cause

With `reset_config none`, OpenOCD resets via software (`SYSRESETREQ` over SWD).
ST-Link V2 `hla_swd` transport frequently times out on this handshake while the
target is running the existing firmware.

With `reset_config srst_only srst_nogate`, OpenOCD drives the hardware NRST line.
This only works if the ST-Link's RST pin is **physically wired** to the target's
NRST. Cheap ST-Link V2 clones often have RST that does not actually connect.

## Which reset mode to set (openocd.cfg)

```
source [find interface/stlink.cfg]
source [find target/stm32f1x.cfg]
```

- NRST **is wired** to target NRST → `reset_config srst_only srst_nogate`
- NRST **not wired** (default / unknown) → `reset_config none`

If `srst_only srst_nogate` times out waiting for halt, treat NRST as not
connected and **revert to `reset_config none`**.

## When a manual reset or BOOT0 is currently required

A setup where manually pressing reset, or holding BOOT0 while programming, is
"needed" is the symptom of the unreliable software reset above. Prefer the
reliable `halt`-first sequence so BOOT0 doesn't matter.

## Reliable flash sequence (avoids reset init / program-reset)

Do NOT rely on `program ... reset exit` (its hidden reset may time out). Instead:

```tcl
# in openocd.cfg of the project
source [find interface/stlink.cfg]
source [find target/stm32f1x.cfg]
reset_config none
```

```
openocd -f openocd.cfg \
  -c "init" \
  -c "halt" \
  -c "program PATH/TO/firmware.elf verify" \
  -c "reset" \
  -c "shutdown"
```

After flashing, press the board's reset button to start the new firmware
(software reset unreliable). If the trailing `reset` still errors, the write
already succeeded — the error is only in resetting.

## When connection fails at init (not reset): "unable to connect to the target"

A second, earlier failure mode exists: OpenOCD fails **before any target
communication** with:

```
Info : Target voltage: 3.28V
Error: init mode failed (unable to connect to the target)
```

This is NOT the reset-step failure above — nothing was flashed, and no `halt`
message appeared. The ST-Link is fine (voltage reads correctly, VID:PID known),
but the MCU does not answer the initial SWD connect. Lowering the adapter speed
(`-c "adapter speed 100"`) usually does **not** help for this symptom.

### Fix: hold the board's reset button during the connect/halt phase

The reliable workaround on this board (ST-Link V2 clone, no wired NRST, RST
button present): **press and hold the board's RESET button** while OpenOCD runs
`init` + `halt`. The target stays in reset so it is not busy running the old
firmware, and the SWD connect/halt succeeds. Once OpenOCD reports the target
halted (`Info : [stm32f1x.cpu] halted due to debug-request ...`), release the
button — the erase/write/program proceeds normally while halted.

This matches the skill's general insight: the problem is that the target is
still running firmware that interferes with the connect handshake; forcing it
into reset removes that interference. If the connect ever fails again with
`unable to connect`, the first thing to reach for is the board reset button, not
adapter speed or `reset_config` changes.

## DAPLink / CMSIS-DAP variant (this board, verified 2026-08-19)

The K2000 firmware **kills SWD as soon as it runs** — a plain `init` fails with
`Error: Error connecting DP: cannot read IDR` unless the target is held in reset
while connecting. The board's NRST is wired (TEL: `NRST ; ... U1.7 ...`), and
DAPLink has **no TRST** (probe reports `nTRST = 0 nRESET = 1`).

So the `openocd.cfg` for this probe is:

```tcl
set WORKAREASIZE 0x100
source [find interface/cmsis-dap.cfg]
source [find target/stm32f1x.cfg]
reset_config srst_only srst_nogate connect_assert_srst
adapter speed 8000
```

and the flash sequence uses `reset halt` (halt while held in reset), NOT `halt`:

```
openocd -f openocd.cfg \
  -c "init" \
  -c "reset halt" \
  -c "program PATH/TO/firmware.elf verify" \
  -c "reset" \
  -c "shutdown"
```

Two non-obvious points:

1. `connect_assert_srst` is what makes the SWD handshake pass on this board.
   With it, a plain `halt` times out (`external reset detected`, `timed out while
   waiting for target halted`); `reset halt` completes at the reset vector
   (`pc: 0xfffffffe`). At `adapter speed 1000`, the halt may land deeper in
   firmware (`pc: 0x08002800`); lowering speed to 100 kHz lands at the reset
   vector — either still programs.

2. `set WORKAREASIZE 0x100` must be set **before** `target/stm32f1x.cfg` is
   sourced. Cheap CMSIS-DAP firmware (Horco `faed:4870`, `FW Version = Horco
   v0.2`) cannot run OpenOCD's work-area async algorithms: the flash **write**
   algorithm and the CRC-verify algorithm time out (`Error: timeout waiting for
   algorithm, a target reset is recommended`, `flash write failed just before
   address 0x8000000`) even though the **erase** algorithm succeeds. Shrinking
   the work area makes OpenOCD fall back to direct single halfword writes
   (`Warn : couldn't use block writes, falling back to single memory accesses`),
   which is slow but reliable — the trailing `verify` then also uses direct
   readback comparison (no CRC errors). Verified: `Programming Finished` +
   `Verified OK`, flash readback matches the .elf bytes exactly. Timing is
   SWD-bound: full 64 KiB flash+verify takes ~31 s at 1 MHz, ~12 s at 4 MHz,
   ~9.4 s at 8 MHz (sweet spot — 10 MHz regresses to ~26 s as the probe
   glitches and retries). Block writes stay broken at every speed; 8 MHz +
   direct-write fallback is the fastest reliable combination on this probe.

## Verification

- Clean flash completes without writing errors; `Preparing Flash` goes through
  even if the final reset step reports trouble.
- Confirm the target halts (`Info : ... halted due to debug-request`).
- If unsure whether NRST is wired, keep `reset_config none` + halt-sequence —
  that is the most portable and the least to sustain physically.
- If `init` itself fails with `unable to connect to the target`, hold the board
  RESET button during `init` + `halt`, then release once halted.