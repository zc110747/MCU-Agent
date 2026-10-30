#!/usr/bin/env python3
"""Exercise the USART1 console command channel and the HID mouse, then print a
PASS/FAIL tally.

    python tools/serial_verify.py --port COM3
    python tools/serial_verify.py --port COM3 --reset      # restart target first

The firmware answers one character per command (see app_ui_command()):

    l / m / r = tap left / middle / right button (150 ms)
    x / y     = nudge the pointer right / down
    c = clear trail     s / ? = status     h = help

Two layers are checked:
  1. the console replies (does the firmware act on the command?)
  2. the host really sees the HID output - the cursor position is sampled with
     user32!GetCursorPos around the x / y commands, and reports=/mounted= come
     from the device's own counters.

Exit code is 0 only when every case passes.
"""

import argparse
import ctypes
import os
import subprocess
import sys
import time

import serial

ELF = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "build", "stm32f429_touchpad.elf")
CONFIG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "openocd.cfg")


def reset_target():
    """Restart the core at the application reset vector.

    On this board a plain `reset run` does not work (no NRST line) and after a
    `reset halt` the core may come up in the ST system-memory bootloader, where
    none of the application runs.  Loading the flash vector by hand is the only
    reliable restart.
    """
    out = subprocess.run(["arm-none-eabi-nm", ELF], capture_output=True, text=True)
    handler = None
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "Reset_Handler":
            handler = parts[0]
    if handler is None:
        raise SystemExit("Reset_Handler not found in %s" % ELF)

    cmd = ("init; reset halt; reg sp 0x20030000; reg pc 0x%s; resume; shutdown"
           % handler)
    subprocess.run(["openocd", "-f", CONFIG, "-c", cmd],
                   capture_output=True, text=True, timeout=60)


class _Point(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int)]


def cursor_pos():
    """Current mouse position, or None when the host API is unavailable."""
    try:
        pt = _Point()
        ctypes.windll.user32.GetCursorPos(ctypes.byref(pt))
        return pt.x, pt.y
    except Exception:  # noqa: BLE001 - not Windows / blocked
        return None


def cursor_home():
    """Park the pointer at a spot with room in both directions.

    Without this the "move down" check fails whenever the cursor already sits
    on the bottom edge of the desktop - which is a property of the test
    environment, not of the firmware.
    """
    try:
        return bool(ctypes.windll.user32.SetCursorPos(200, 200))
    except Exception:  # noqa: BLE001
        return False


def ask(ser, cmd, wait=1.2):
    """Send one command character, return whatever the board answers."""
    ser.reset_input_buffer()
    ser.write(cmd.encode())
    ser.flush()
    time.sleep(wait)
    return ser.read(1024).decode("utf-8", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--reset", action="store_true",
                    help="restart the target before testing")
    args = ap.parse_args()

    if args.reset:
        print("resetting target ...", flush=True)
        reset_target()
        time.sleep(3.0)          # let lcd/sd/font/usb init finish

    try:
        ser = serial.Serial(args.port, 115200, timeout=0.2)
    except Exception as exc:  # noqa: BLE001
        print("cannot open %s: %s" % (args.port, exc))
        return 2

    cases = [
        ("help",           "h", ["[CMD ] l/m/r=tap button", "s=status"]),
        ("usb-mounted",    "s", ["[USB ] mounted=1"]),
        ("usb-reports",    "s", ["[USB ] mounted=1", "reports="]),
        ("status-move",    "s", ["[MOVE] x="]),
        ("status-sd",      "s", ["[SD  ] 0: mounted"]),
        ("status-font",    "s", ["[FONT] src=0:", "mask=0x1F"]),
        ("status-touch",   "s", ["[TOUCH] irq=", "ready=1"]),
        ("status-screen",  "s", ["[UI  ] screen=1"]),
        ("click-left",     "l", ["[CMD ] left click"]),
        ("click-middle",   "m", ["[CMD ] middle click"]),
        ("click-right",    "r", ["[CMD ] right click"]),
        ("move-right",     "x", ["[CMD ] move right"]),
        ("move-down",      "y", ["[CMD ] move down"]),
        ("clear-trail",    "c", ["[CMD ] trail cleared"]),
        ("unknown-cmd",    "q", ["[CMD ] unknown 'q'"]),
    ]

    passed = 0
    failed = 0
    print("%-16s %-4s %s" % ("case", "res", "detail"))
    print("-" * 68)

    for name, cmd, needles in cases:
        reply = ask(ser, cmd)
        missing = [n for n in needles if n not in reply]
        if missing:
            failed += 1
            print("%-16s FAIL missing %s" % (name, missing))
            if reply:
                print("                 got: %r" % reply[:120])
        else:
            passed += 1
            first = reply.strip().splitlines()[0] if reply.strip() else ""
            print("%-16s ok   %s" % (name, first[:60]))

    # ---- Host-side proof: does the cursor actually move? -------------------
    print("-" * 68)
    homed = cursor_home()
    time.sleep(0.3)
    before = cursor_pos()
    if before is None:
        print("%-16s SKIP GetCursorPos unavailable on this host" % "cursor-move")
    else:
        ask(ser, "x", wait=0.8)
        after_x = cursor_pos()
        ask(ser, "y", wait=0.8)
        after_y = cursor_pos()

        moved_x = (after_x is not None) and (after_x[0] > before[0])
        moved_y = (after_y is not None) and (after_y[1] > after_x[1])

        for label, ok, detail in (
            ("cursor-right", moved_x, "%s -> %s" % (before, after_x)),
            ("cursor-down", moved_y, "%s -> %s" % (after_x, after_y)),
        ):
            if ok:
                passed += 1
                print("%-16s ok   %s" % (label, detail))
            elif not homed:
                # No SetCursorPos: the pointer may already be on an edge, so a
                # no-op is not evidence of a firmware fault.
                print("%-16s SKIP %s (could not park the pointer)" % (label, detail))
            else:
                failed += 1
                print("%-16s FAIL %s" % (label, detail))

    ser.close()
    print("-" * 68)
    print("PASS %d / FAIL %d" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
