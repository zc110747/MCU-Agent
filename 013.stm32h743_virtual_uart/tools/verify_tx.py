#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
PC-side acceptance for the STM32H743 software UART.

No logic analyser is connected, so correctness is judged by what the PC
actually receives on the ST-Link virtual COM port (PA9/PA10).

Phase 2/3 - TX only: read a while, then check the received stream matches the
pattern the firmware transmits (0x55 0xAA 0x00 0xFF repeating).

Usage:
    python verify_tx.py --port COM19 --baud 9600 --seconds 5
    python verify_tx.py --port COM19 --expect-echo --seconds 5      # phase 6
"""

import argparse
import sys
import time
from collections import Counter

try:
    import serial
except ImportError:
    print("ERROR: pyserial missing. Install into the system Python:")
    print("  D:/Software/Python3/python.exe -m pip install pyserial")
    sys.exit(2)


PATTERN = bytes([0x55, 0xAA, 0x00, 0xFF])


def parse_args():
    ap = argparse.ArgumentParser(description="software UART TX verification")
    ap.add_argument("--port", default="COM19")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--expect-echo", action="store_true",
                    help="expect the sent bytes back (Phase 6 echo test)")
    ap.add_argument("--tx-bytes", type=int, default=0,
                    help="with --expect-echo: how many bytes to send")
    ap.add_argument("--quiet", action="store_true")
    return ap.parse_args()


def main():
    args = parse_args()
    fails = []

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.05,
                            write_timeout=2.0)
    except Exception as exc:                       # noqa: BLE001
        print(f"FAIL  open {args.port}: {exc}")
        return 2

    if not args.quiet:
        print(f"[info] {args.port} @ {args.baud} 8N1")

    try:
        if args.expect_echo:
            run_echo(ser, args, fails)
        else:
            run_tx(ser, args, fails)
    finally:
        ser.close()

    print()
    if fails:
        print(f"RESULT: FAIL ({len(fails)} failed checks)")
        for f in fails:
            print(f"  - {f}")
        return 1

    print("RESULT: PASS (0 failed checks)")
    return 0


def run_tx(ser, args, fails):
    """Read the firmware's pattern stream and judge it."""
    ser.reset_input_buffer()
    deadline = time.time() + args.seconds
    got = bytearray()

    while time.time() < deadline:
        chunk = ser.read(4096)
        if chunk:
            got += chunk

    if not got:
        fails.append("no bytes received at all")
        return

    counts = Counter(got)
    if not args.quiet:
        print(f"[rx] {len(got)} bytes")
        for b in PATTERN:
            print(f"      0x{b:02X}: {counts.get(b, 0)}")
        other = {k: v for k, v in counts.items() if k not in PATTERN}
        if other:
            print(f"      unexpected bytes: "
                  f"{ {hex(k): v for k, v in list(other.items())[:8]} }")

    for b in PATTERN:
        if counts.get(b, 0) == 0:
            fails.append(f"pattern byte 0x{b:02X} never arrived")

    if other:
        bad = sum(v for k, v in other.items() if k not in PATTERN)
        fails.append(f"{bad} bytes outside the expected pattern "
                     f"({ {hex(k): v for k, v in list(other.items())[:4]} })")

    # The firmware repeats 0x55,0xAA,0x00,0xFF, so a healthy stream is close
    # to evenly split. A large skew means bits were lost or doubled up.
    for b in PATTERN:
        share = counts.get(b, 0) / len(got)
        if share < 0.15:
            fails.append(f"0x{b:02X} share only {share:.1%}, "
                         f"expected roughly 25% - bit errors likely")

    if not args.quiet and not fails:
        print("[ok] stream matches the expected repeating pattern")


def run_echo(ser, args, fails):
    """Phase 6: send known bytes and require them back unchanged."""
    n = args.tx_bytes or 2000
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    payload = bytes((i * 7 + 3) & 0xFF for i in range(n))
    sent = 0
    t0 = time.time()
    while sent < n:
        k = ser.write(payload[sent:sent + 256])
        if not k:
            break
        sent += k
    ser.flush()

    rx = bytearray()
    deadline = time.time() + args.seconds
    while time.time() < deadline and len(rx) < sent:
        chunk = ser.read(4096)
        if chunk:
            rx += chunk
        elif time.time() - t0 > args.seconds * 0.9:
            break

    if not args.quiet:
        print(f"[echo] sent {sent}, received {len(rx)} in "
              f"{time.time() - t0:.2f}s")

    if sent != n:
        fails.append(f"host could only push {sent} of {n} bytes")
    if len(rx) != sent:
        fails.append(f"length mismatch: sent {sent}, received {len(rx)}")
    else:
        bad = [i for i in range(sent) if rx[i] != payload[i]]
        if bad:
            fails.append(f"{len(bad)} corrupted bytes, first at "
                         f"offset {bad[0]}")
        elif not args.quiet:
            print("[ok] every byte returned unchanged")


if __name__ == "__main__":
    sys.exit(main())
