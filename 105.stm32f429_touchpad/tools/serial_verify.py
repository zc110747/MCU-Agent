#!/usr/bin/env python3
"""Exercise the USART1 console command channel and print a PASS/FAIL tally.

    python tools/serial_verify.py --port COM3
    python tools/serial_verify.py --port COM3 --reset      # restart target first

The firmware answers one character per command (see app_ui_command()):

    c = clear   k = next colour   w = next width
    b = toggle wallpaper         s / ? = status   h = help

Exit code is 0 only when every case passes.
"""

import argparse
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
        time.sleep(2.5)          # let lcd/sd/font init finish

    try:
        ser = serial.Serial(args.port, 115200, timeout=0.2)
    except Exception as exc:  # noqa: BLE001
        print("cannot open %s: %s" % (args.port, exc))
        return 2

    cases = [
        ("help",           "h", ["[CMD ] c=clear", "s=status"]),
        ("status-sd",      "s", ["[SD  ] 0: mounted"]),
        ("status-font",    "s", ["[FONT] src=0:", "mask=0x1F"]),
        ("status-touch",   "s", ["[TOUCH] irq=", "ready=1"]),
        ("status-gest",    "s", ["[GEST] fingers="]),
        ("status-brush",   "s", ["[BRUSH] color="]),
        ("status-screen",  "s", ["[UI  ] screen=1"]),
        ("clear",          "c", ["[CMD ] clear"]),
        ("colour",         "k", ["[CMD ] colour ->"]),
        ("width",          "w", ["[CMD ] width ->"]),
        ("wallpaper",      "b", ["[CMD ] wallpaper toggle"]),
        ("unknown-cmd",    "x", ["[CMD ] unknown 'x'"]),
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

    ser.close()
    print("-" * 68)
    print("PASS %d / FAIL %d" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
