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

## Verification

- Clean flash completes without writing errors; `Preparing Flash` goes through
  even if the final reset step reports trouble.
- Confirm the target halts (`Info : ... halted due to debug-request`).
- If unsure whether NRST is wired, keep `reset_config none` + halt-sequence —
  that is the most portable and the least to sustain physically.