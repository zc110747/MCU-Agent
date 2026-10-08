#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
PC-side acceptance for Phase 3 of the STM32H743 software UART.

The firmware streams an incrementing byte pattern (0,1,2,...,255,0,1,...), so a
correct TX FIFO arrives as a stream in which every neighbouring pair steps by
exactly +1 modulo 256. If the ring wrapped incorrectly - overwriting unread
bytes, or re-emitting stale ones - those steps would break in bursts and the
longest unbroken run would collapse.

Checks
    * bytes actually arrive
    * the longest unbroken +1 run is long enough to have crossed the 4096-byte
      ring boundary (T5 alone streams two full rings)
    * the +1 proportion is essentially perfect

Usage:
    python verify_phase3.py --port COM19 --baud 9600 --seconds 20
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial missing. Install into the system Python:")
    print("  D:/Software/Python3/python.exe -m pip install pyserial")
    sys.exit(2)


MIN_BYTES = 4000        # a healthy 9600 8N1 stream yields ~960 B/s
MIN_RUN = 4096          # T5 streams 8192 wrapped bytes; the ring is 4096
MIN_STEP_RATIO = 0.999


def parse_args():
    ap = argparse.ArgumentParser(description="software UART Phase 3 verification")
    ap.add_argument("--port", default="COM19")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--quiet", action="store_true")
    return ap.parse_args()


def scan_plus_one(data):
    """Longest run of +1 (mod 256) steps, plus the step tally."""
    best = 0
    cur = 1 if data else 0
    good = 0
    steps = 0

    for i in range(1, len(data)):
        steps += 1
        if (data[i] - data[i - 1]) % 256 == 1:
            cur += 1
            good += 1
        else:
            if cur > best:
                best = cur
            cur = 1

    if cur > best:
        best = cur
    return best, good, steps


def main():
    args = parse_args()
    fails = []

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.05)
    except Exception as exc:                       # noqa: BLE001
        print(f"FAIL  open {args.port}: {exc}")
        return 2

    data = bytearray()
    try:
        if not args.quiet:
            print(f"[info] {args.port} @ {args.baud} 8N1, "
                  f"reading {args.seconds:.0f}s")
        deadline = time.time() + args.seconds
        while time.time() < deadline:
            chunk = ser.read(8192)
            if chunk:
                data += chunk
    finally:
        ser.close()

    if len(data) < MIN_BYTES:
        fails.append(f"only {len(data)} bytes received (need {MIN_BYTES})")
    else:
        run, good, steps = scan_plus_one(data)
        ratio = good / steps if steps else 0.0

        if not args.quiet:
            print(f"[rx]   {len(data)} bytes")
            print(f"[run]  longest +1 run: {run}")
            print(f"[step] +1 pairs: {good}/{steps} = {ratio:.4%}")

        if run < MIN_RUN:
            fails.append(f"longest +1 run {run} < {MIN_RUN}: "
                         f"ring order broken around a wrap")
        if ratio < MIN_STEP_RATIO:
            fails.append(f"+1 proportion {ratio:.4%} < {MIN_STEP_RATIO:.4%}")

    print()
    if fails:
        print(f"RESULT: FAIL ({len(fails)} failed checks)")
        for f in fails:
            print(f"  - {f}")
        return 1

    print("RESULT: PASS (0 failed checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
