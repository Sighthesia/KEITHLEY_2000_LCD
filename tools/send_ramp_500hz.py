#!/usr/bin/env python3
"""Send a ramping Keithley reading stream for display stress testing.

The requested sample cadence is 500 Hz, but the instrument protocol runs at
9600 8N1. The script therefore keeps the 2 ms generation schedule and reports
the actual wire rate separately when serial writes block behind the UART.
"""

import argparse
import time

import serial
from serial import SerialTimeoutException


FRAME_PREFIX = b"\x0d\x01"
FRAME_SUFFIX = b"\x0d"


def make_frame(value_mv: float) -> bytes:
    reading = f"{value_mv:+09.4f}mVDC.".encode("ascii")
    return FRAME_PREFIX + reading + FRAME_SUFFIX


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baudrate", type=int, default=9600)
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--frequency", type=float, default=500.0)
    parser.add_argument("--peak-mv", type=float, default=49.5753)
    parser.add_argument("--block", action="store_true",
                        help="wait for the UART instead of dropping busy samples")
    args = parser.parse_args()

    if args.frequency <= 0.0 or args.duration <= 0.0 or args.peak_mv <= 0.0:
        parser.error("frequency, duration and peak-mv must be positive")

    period = 1.0 / args.frequency
    total = int(args.duration * args.frequency)
    sent = 0
    dropped = 0
    started = time.monotonic()
    next_deadline = started

    print(
        f"sending {total} generated frames at {args.frequency:.1f} Hz "
        f"for {args.duration:.1f}s on {args.port}@{args.baudrate}"
    )
    print("payload example: -049.5753mVDC. (trailing dot = trigger)")

    try:
        with serial.Serial(args.port, args.baudrate, bytesize=8, parity="N",
                           stopbits=1, timeout=1.0,
                           write_timeout=None if args.block else 0.001) as port:
            for index in range(total):
                phase = index % 2000
                position = phase if phase <= 1000 else 2000 - phase
                value = -args.peak_mv + (2.0 * args.peak_mv * position / 1000.0)
                try:
                    port.write(make_frame(value))
                    sent += 1
                except SerialTimeoutException:
                    dropped += 1
                next_deadline += period
                delay = next_deadline - time.monotonic()
                if delay > 0.0:
                    time.sleep(delay)
    except KeyboardInterrupt:
        print("stopped by user")
    finally:
        elapsed = max(time.monotonic() - started, 1e-9)
        generated = sent + dropped
        print(f"generated={generated} sent={sent} dropped={dropped} "
              f"elapsed={elapsed:.3f}s wire={sent / elapsed:.1f} Hz")
        print(f"wire_limit≈{args.baudrate / (len(make_frame(0.0)) * 10):.1f} frames/s")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
