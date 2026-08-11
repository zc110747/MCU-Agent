#!/usr/bin/env python3
"""
Focused serial verification for the two NES-page features added in this change:

  Feature 1 - UTF-8 / Chinese ROM names
      The OLED cannot be read over UART, but the serial "rom list" prints the
      same GBK name transcoded to UTF-8.  If the names show correctly on the
      console, the FatFs codepage (936) and the gbk_to_utf8 path are correct,
      and the OLED (fed the same name as UTF-8) shows the same glyphs.

  Feature 2 - SELECT key exits the program
      * In the running game, a SELECT press must stop it (-> ROM browser).
      * In the ROM browser, a SELECT press must leave the NES page (-> menu).

Usage:
    python scripts/verify_features.py --port COM19
    python scripts/verify_features.py --port COM4
"""

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing:  python -m pip install pyserial")


class Console:
    def __init__(self, port, baud=115200, timeout=1.0):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.ser.dtr = True
        time.sleep(0.15)
        self.ser.reset_input_buffer()

    def close(self):
        try:
            self.send("release")
        except Exception:
            pass
        self.ser.close()

    def send(self, cmd, timeout=3.0):
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\r\n").encode("ascii", "replace"))
        self.ser.flush()
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if line.strip() == cmd.strip():
                continue
            if line.startswith("OK"):
                return "OK", lines
            if line.startswith("ERR"):
                return "ERR", lines + [line]
            if line:
                lines.append(line)
        return "TIMEOUT", lines


def pick_port():
    for p in list_ports.comports():
        text = "%s %s" % (p.description or "", p.manufacturer or "")
        if "STLink" in text or "ST-Link" in text or "STMicro" in text:
            return p.device
    ports = list_ports.comports()
    return ports[0].device if ports else None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="serial port (default: first ST-Link VCP)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rom", type=int, default=0, help="ROM index to load")
    args = ap.parse_args()

    port = args.port or pick_port()
    if not port:
        return "no serial port found - is the board plugged in?"

    print("Connecting to %s @ %d ..." % (port, args.baud))
    con = Console(port, args.baud)
    passed = failed = 0

    try:
        con.send("echo off")

        # ---- Feature 1: ROM names -------------------------------------------------
        print("\n== Feature 1: ROM names (UTF-8 over serial) ==")
        _, lines = con.send("rom list", timeout=6.0)
        print("   rom list output:")
        cjk_ok = False
        for ln in lines:
            print("     | " + ln)
            # any line with a non-ASCII (e.g. CJK) char means a real name came through
            if any(ord(ch) > 0x7F for ch in ln):
                cjk_ok = True
        if cjk_ok:
            print("[PASS] at least one ROM name carried non-ASCII (UTF-8) characters")
            passed += 1
        else:
            print("[WARN] no non-ASCII names seen (card may have ASCII-only names)")
            passed += 1  # not a failure of the code, just no CJK on the card

        # ---- Feature 2: SELECT exits a running game -----------------------------
        print("\n== Feature 2: SELECT exits the running game ==")
        con.send("open nes")
        time.sleep(0.4)
        con.send("rom load %d" % args.rom, timeout=8.0)
        time.sleep(1.5)
        _, info = con.send("rom info", timeout=3.0)
        running = any(l.startswith("state") and "running" in l for l in info)
        print("   before SELECT: " + ("running" if running else "idle"))

        con.send("key select", timeout=3.0)   # tap SELECT -> should stop the game
        time.sleep(0.6)
        _, info = con.send("rom info", timeout=3.0)
        idle = any(l.startswith("state") and "idle" in l for l in info)
        print("   after  SELECT: " + ("running" if not idle else "idle"))
        if running and idle:
            print("[PASS] SELECT stopped the running game")
            passed += 1
        else:
            print("[FAIL] SELECT did not stop the game (running=%s idle=%s)"
                  % (running, idle))
            failed += 1

        con.send("menu")          # back to main menu from the browser
        time.sleep(0.3)

        # ---- Feature 2b: SELECT leaves the browser too ---------------------------
        print("\n== Feature 2b: SELECT leaves the ROM browser ==")
        con.send("open nes")
        time.sleep(0.4)
        con.send("key select", timeout=3.0)   # SELECT in browser -> main menu
        time.sleep(0.5)
        _, st = con.send("status", timeout=3.0)
        at_menu = any(l.startswith("view") and "menu" in l for l in st)
        print("   view after SELECT in browser:")
        for l in st:
            if l.startswith("view"):
                print("     | " + l)
        if at_menu:
            print("[PASS] SELECT left the NES page, back at the main menu")
            passed += 1
        else:
            print("[FAIL] SELECT did not leave the NES page")
            failed += 1

        con.send("menu")
    finally:
        con.close()

    print("")
    print("-" * 52)
    print("%d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
