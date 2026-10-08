#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
PC-side acceptance for Phase 4/5 (RX) of the STM32H743 software UART.

There is no logic analyser, so the RX path is judged from the outside: the
host pushes a known octet pattern out of the port, the firmware drains it and
hands back a 40-byte record (counts, overflow, frame errors, sum, FNV-1a hash,
first/last byte). The host recomputes the same digest over the bytes it sent
and compares - a dropped, duplicated or reordered byte changes both the count
and the digest.

Usage:
    python verify_rx.py --port COM19 --baud 9600 --expected 4096
    python verify_rx.py --pattern ff          # worst case for edges
    python verify_rx.py --no-reset            # do not reset via OpenOCD first
"""

import argparse
import struct
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("ERROR: pyserial missing. Install into the system Python:")
    print("  D:/Software/Python3/python.exe -m pip install pyserial")
    sys.exit(2)

MAGIC = 0x52583454            # "RX4T" - first word of the record
RECORD_WORDS = 10
RECORD_BYTES = RECORD_WORDS * 4
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619

ROOT = Path(__file__).resolve().parent.parent
OOCD_CFG = ROOT / "openocd" / "stm32h743_stlink.cfg"


def parse_args():
    ap = argparse.ArgumentParser(description="software UART Phase 4/5 RX verification")
    ap.add_argument("--port", default="COM19")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--expected", type=int, default=4096)
    ap.add_argument("--pattern", default="lcg",
                    choices=["lcg", "ff", "55", "aa", "zero"],
                    help="stimulus pattern (lcg is pseudo-random)")
    ap.add_argument("--wait", type=float, default=30.0,
                    help="seconds to wait for the record after the last byte")
    ap.add_argument("--no-reset", action="store_true",
                    help="skip the OpenOCD reset that restarts the run")
    return ap.parse_args()


def make_pattern(kind, n):
    if kind == "ff":
        return bytes([0xFF]) * n
    if kind == "55":
        return bytes([0x55]) * n
    if kind == "aa":
        return bytes([0xAA]) * n
    if kind == "zero":
        return bytes([0x00]) * n
    out = bytearray()
    x = 0x12345678
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


def sum32(data):
    return sum(data) & 0xFFFFFFFF


def fnv1a(data):
    h = FNV_OFFSET
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h


def reset_target():
    try:
        subprocess.run(
            ["openocd", "-f", str(OOCD_CFG),
             "-c", "init", "-c", "reset run", "-c", "exit"],
            capture_output=True, timeout=20, check=True)
    except Exception as exc:                       # noqa: BLE001
        print(f"FAIL  openocd reset: {exc}")
        return False
    return True


def find_record(stream):
    """Return the 10 decoded words once the magic shows up."""
    for i in range(0, len(stream) - RECORD_BYTES + 1):
        if struct.unpack_from("<I", stream, i)[0] == MAGIC:
            return struct.unpack_from("<%dI" % RECORD_WORDS, stream, i), i
    return None, -1


def main():
    args = parse_args()
    fails = []

    if not args.no_reset and not reset_target():
        return 2

    pattern = make_pattern(args.pattern, args.expected)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except Exception as exc:                       # noqa: BLE001
        print(f"FAIL  open {args.port}: {exc}")
        return 2

    stream = bytearray()
    try:
        time.sleep(0.2)
        ser.reset_input_buffer()
        print(f"[info] {args.port} @ {args.baud} 8N1, sending {len(pattern)} bytes"
              f" ({args.pattern})")
        ser.write(pattern)
        ser.flush()

        deadline = time.time() + args.wait
        while time.time() < deadline:
            chunk = ser.read(256)
            if chunk:
                stream += chunk
                rec, _ = find_record(stream)
                if rec is not None:
                    break
    finally:
        ser.close()

    rec, at = find_record(stream)
    if rec is None:
        print(f"FAIL  no record found ({len(stream)} bytes read back)")
        return 1

    (magic, expected, received, overflow, frame_error,
     csum, chash, first, last, done) = rec

    want_sum = sum32(pattern)
    want_hash = fnv1a(pattern)

    print(f"[rec] at offset {at}: magic=0x{magic:08X} expected={expected} "
          f"received={received} done={done}")
    print(f"[err] overflow={overflow} frame_error={frame_error}")
    print(f"[dig] sum=0x{csum:08X} (want 0x{want_sum:08X})  "
          f"hash=0x{chash:08X} (want 0x{want_hash:08X})")
    print(f"[end] first={first} (want {pattern[0]})  "
          f"last={last} (want {pattern[-1]})")

    if done != 1:
        fails.append("record says the capture did not finish")
    if received != args.expected:
        fails.append(f"received {received} != expected {args.expected}")
    if overflow != 0:
        fails.append(f"{overflow} bytes lost to RX FIFO overflow")
    if frame_error != 0:
        fails.append(f"{frame_error} frame errors (start/stop bit violated)")
    if csum != want_sum:
        fails.append(f"sum mismatch: 0x{csum:08X} != 0x{want_sum:08X}")
    if chash != want_hash:
        fails.append(f"hash mismatch: 0x{chash:08X} != 0x{want_hash:08X}")
    if first != pattern[0]:
        fails.append(f"first byte {first} != {pattern[0]}")
    if last != pattern[-1]:
        fails.append(f"last byte {last} != {pattern[-1]}")

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
