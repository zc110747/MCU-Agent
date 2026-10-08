#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
PC-side acceptance for Phase 6/7 (echo) of the STM32H743 software UART.

Full duplex: a reader thread drains the port continuously while the main
thread pushes a known pattern out of it. The firmware echoes every received
byte straight back, so the trace the host gets must be the pattern it sent,
in order - a dropped, duplicated or reordered byte shows up as a length
mismatch or a byte mismatch.

The on-chip counters are read back over SWD afterwards as corroboration: the
loopback proves the bytes, the counters prove the FIFOs never overflowed or
rejected anything along the way.

Usage:
    python verify_echo.py --bytes 4096
    python verify_echo.py --bytes 100000 --pattern lcg
    python verify_echo.py --no-probe            # skip the SWD read-back
"""

import argparse
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("ERROR: pyserial missing. Install into the system Python:")
    print("  D:/Software/Python3/python.exe -m pip install pyserial")
    sys.exit(2)

ROOT = Path(__file__).resolve().parent.parent
OOCD_CFG = ROOT / "openocd" / "stm32h743_stlink.cfg"
DEFAULT_ELF = ROOT / "build" / "debug" / "h743_virtual_uart.elf"

PROBE_MAGIC = 0x45434854        # "ECHT"
PROBE_NAMES = ["magic", "echoed", "rx_bytes", "tx_bytes",
               "rx_overflow", "rx_frame_error", "tx_overflow", "loops"]


def parse_args():
    ap = argparse.ArgumentParser(description="software UART Phase 6/7 echo verification")
    ap.add_argument("--port", default="COM19")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--bytes", type=int, default=4096)
    ap.add_argument("--pattern", default="lcg",
                    choices=["lcg", "ff", "00", "55", "aa"])
    ap.add_argument("--timeout", type=float, default=180.0,
                    help="seconds to wait for the whole echo")
    ap.add_argument("--chunk", type=int, default=256,
                    help="host write granularity")
    ap.add_argument("--elf", default=str(DEFAULT_ELF))
    ap.add_argument("--no-probe", action="store_true")
    ap.add_argument("--no-reset", action="store_true")
    return ap.parse_args()


def make_pattern(kind, n):
    if kind == "ff":
        return bytes([0xFF]) * n
    if kind == "00":
        return bytes([0x00]) * n
    if kind == "55":
        return bytes([0x55]) * n
    if kind == "aa":
        return bytes([0xAA]) * n
    out = bytearray()
    x = 0x12345678
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


def reset_target():
    try:
        subprocess.run(["openocd", "-f", str(OOCD_CFG),
                        "-c", "init", "-c", "reset run", "-c", "exit"],
                       capture_output=True, timeout=20, check=True)
    except Exception as exc:                       # noqa: BLE001
        print(f"FAIL  openocd reset: {exc}")
        return False
    return True


def probe_address(elf):
    try:
        out = subprocess.run(["arm-none-eabi-nm", elf],
                             capture_output=True, text=True, timeout=20).stdout
    except Exception:                              # noqa: BLE001
        return None
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "g_echo":
            return int(parts[0], 16)
    return None


def read_probe(addr, words):
    cmd = ["openocd", "-f", str(OOCD_CFG),
           "-c", "init", "-c", "halt",
           "-c", "mdw 0x%08X %d" % (addr, words),
           "-c", "resume", "-c", "exit"]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=40)
    # openocd prints the mdw dump on stderr alongside its log lines, so both
    # streams have to be scanned.
    text = (out.stdout or "") + "\n" + (out.stderr or "")
    vals = []
    for line in text.splitlines():
        if ":" not in line or not line.lstrip().startswith("0x"):
            continue
        for tok in line.split(":", 1)[1].split():
            try:
                vals.append(int(tok, 16))
            except ValueError:
                pass
    return vals


def main():
    args = parse_args()
    fails = []

    if not args.no_reset and not reset_target():
        return 2

    pattern = make_pattern(args.pattern, args.bytes)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.05)
    except Exception as exc:                       # noqa: BLE001
        print(f"FAIL  open {args.port}: {exc}")
        return 2

    got = bytearray()
    stop = threading.Event()

    def reader():
        while not stop.is_set():
            try:
                chunk = ser.read(4096)
            except Exception:                      # noqa: BLE001
                break
            if chunk:
                got.extend(chunk)

    th = threading.Thread(target=reader, daemon=True)
    th.start()

    try:
        time.sleep(0.2)
        print(f"[info] {args.port} @ {args.baud} 8N1, echoing {args.bytes} bytes"
              f" ({args.pattern})")
        t0 = time.time()
        off = 0
        while off < len(pattern):
            n = min(args.chunk, len(pattern) - off)
            ser.write(pattern[off:off + n])
            off += n
        ser.flush()

        deadline = time.time() + args.timeout
        while time.time() < deadline:
            if len(got) >= args.bytes:
                break
            time.sleep(0.05)
        elapsed = time.time() - t0
    finally:
        stop.set()
        th.join(timeout=2.0)
        ser.close()

    # ------------------------------------------------------------------ copy
    echo = bytes(got)
    print(f"[rx]   echoed {len(echo)} of {args.bytes} bytes in {elapsed:.1f}s")

    if len(echo) != args.bytes:
        fails.append(f"echoed {len(echo)} bytes, expected {args.bytes}")
    mismatches = 0
    first_bad = -1
    for i in range(min(len(echo), args.bytes)):
        if echo[i] != pattern[i]:
            mismatches += 1
            if first_bad < 0:
                first_bad = i
    if mismatches:
        fails.append(f"{mismatches} byte mismatches, first at index {first_bad} "
                     f"(got {echo[first_bad]}, want {pattern[first_bad]})")
    print(f"[cmp] byte mismatches: {mismatches}")

    # ------------------------------------------------------------ on-chip
    if not args.no_probe:
        addr = probe_address(args.elf)
        if addr is None:
            print("[probe] symbol g_echo not found - skipped")
        else:
            vals = read_probe(addr, len(PROBE_NAMES))
            probe = dict(zip(PROBE_NAMES, vals))
            print(f"[probe] @ 0x{addr:08X}: " +
                  " ".join(f"{k}={v}" for k, v in probe.items()))
            if len(vals) < len(PROBE_NAMES):
                fails.append("probe read back short")
            else:
                if probe["magic"] != PROBE_MAGIC:
                    fails.append(f"probe magic 0x{probe['magic']:08X} wrong")
                if probe["echoed"] != args.bytes:
                    fails.append(f"probe echoed {probe['echoed']} != {args.bytes}")
                if probe["rx_bytes"] != args.bytes:
                    fails.append(f"probe rx_bytes {probe['rx_bytes']} != {args.bytes}")
                if probe["rx_overflow"] != 0:
                    fails.append(f"RX FIFO overflowed by {probe['rx_overflow']}")
                if probe["rx_frame_error"] != 0:
                    fails.append(f"{probe['rx_frame_error']} RX frame errors")

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
