#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
auto_verify.py -- hands-off verification of the STM32F429 music player over the
debug serial port (USART3 == COM7 on the host).

It drives the SAME command set a human would use on the LCD (see app/serial_cmd.c)
and asserts the board stays responsive and fault-free, so no person and no
ST-Link/OpenOCD is needed during the test:

  * every key is simulated by a serial command (p/n/v/x/+/-/s/k/t)
  * a 'd' command makes the BOARD itself print CFSR/HFSR -- a non-zero CFSR
    means it would have frozen, so we catch the click-to-freeze regression
  * a 'zNN' stress loop hammers toggle/next/prev/pause/stop NN times

PASS criteria: every command gets its echo AND every CFSR read is 0x00000000.

Usage:
    python auto_verify.py [COM7] [baud]
"""

import sys
import time
import re
import serial


PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

# (command, expected-substring-or-None, human label)
SCRIPT = [
    ("?",   "[SER ] cmds:",            "help"),
    ("n",   "[CMD] next",              "next"),
    ("v",   "[CMD] prev",              "prev"),
    ("p",   "[CMD] toggle",            "play (start DMA)"),
    ("d",   "[DIAG] CFSR=0x00000000",  "health-after-play (must NOT freeze)"),
    ("p",   "[CMD] toggle",            "pause"),
    ("+",   "[CMD] vol+",              "vol+"),
    ("-",   "[CMD] vol-",              "vol-"),
    ("k30", "[CMD] seek 30%",          "seek 30%"),
    ("s",   "[CMD] seek 50%",          "seek 50%"),
    ("x",   "[CMD] stop",              "stop"),
]


def read_until(ser, expect, timeout=8.0):
    """Read lines until `expect` (substring) appears or timeout. Returns
    (found, text)."""
    end = time.time() + timeout
    buf = ""
    while time.time() < end:
        try:
            chunk = ser.read(ser.in_waiting or 1)
        except Exception:
            chunk = b""
        if chunk:
            buf += chunk.decode("latin1", "replace")
            if expect is None or expect in buf:
                return True, buf
    return False, buf


def cfsr_of(text):
    m = re.search(r"CFSR=0x([0-9A-Fa-f]{8})", text)
    return int(m.group(1), 16) if m else None


def main():
    print("[VER] opening %s @ %d" % (PORT, BAUD))
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
    time.sleep(0.4)
    ser.reset_input_buffer()

    passed = 0
    failed = 0
    fails = []

    # boot banner
    ok, banner = read_until(ser, "[SER ] console ready", timeout=12.0)
    if ok:
        print("[VER] boot OK: console ready")
    else:
        print("[VER] WARN: no console-ready banner (continuing): %r" % banner[-120:])

    for cmd, expect, label in SCRIPT:
        ser.write((cmd + "\n").encode("ascii"))
        found, text = read_until(ser, expect, timeout=8.0)
        if found:
            passed += 1
            print("[VER] PASS  %-22s -> %s" % (label, expect))
        else:
            failed += 1
            fails.append((cmd, label, text[-160:]))
            print("[VER] FAIL  %-22s (no %s)" % (label, expect))
            print("        last output: %r" % text[-160:])
        # If the board froze (e.g. on play), later commands will also fail;
        # stop early once a health read shows a fault or stops responding.
        if cmd == "d" and found:
            c = cfsr_of(text)
            if c not in (0, None):
                print("[VER] FATAL: CFSR=0x%08X -> board faulted on play" % c)
                break

    # ---- stress loop ----
    ser.write(b"z20\n")
    ok, text = read_until(ser, "[STR] stress done", timeout=30.0)
    cycles = text.count("[STR] cycle=")
    if ok and cycles >= 20:
        passed += 1
        print("[VER] PASS  stress 20 cycles completed (counted %d)" % cycles)
    else:
        failed += 1
        fails.append(("z20", "stress", text[-160:]))
        print("[VER] FAIL  stress (done=%s, cycles=%d)" % (ok, cycles))

    # final health
    ser.write(b"d\n")
    ok, text = read_until(ser, "[DIAG]", timeout=8.0)
    c = cfsr_of(text)
    if ok and c == 0:
        passed += 1
        print("[VER] PASS  final health CFSR=0x00000000")
    else:
        failed += 1
        fails.append(("d", "final-health", text[-160:]))
        print("[VER] FAIL  final health CFSR=%s" % (("0x%08X" % c) if c is not None else "n/a"))

    ser.close()

    print("=" * 60)
    print("[VER] RESULT: %d passed, %d failed" % (passed, failed))
    if failed == 0:
        print("[VER] VERDICT: PASS  (no freeze, no fault, all keys simulated OK)")
        return 0
    print("[VER] VERDICT: FAIL")
    for cmd, label, tail in fails:
        print("   - %s (%s): %r" % (cmd, label, tail))
    return 1


if __name__ == "__main__":
    sys.exit(main())
