#!/usr/bin/env python3
"""Capture the USART log of the touchpad demo across a target reset.

Usage:
    python tools/serial_capture.py --ports COM3 COM13 --seconds 12

The script opens every candidate port, then asks OpenOCD to reset the target
so the full boot sequence is captured no matter which port the board wires
USART3 to.  Output goes to stdout, prefixed with the port name.
"""

import argparse
import os
import subprocess
import sys
import threading
import time

import serial

BANNER = "==== %s ===="

ELF = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "build", "stm32f429_touchpad.elf")
CONFIG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "openocd.cfg")


def reset_target():
    """Restart the core at the application reset vector.

    `reset run` needs the NRST line, which is not wired here, and after a plain
    `reset halt` the core can come up in the ST system-memory bootloader
    (VTOR = 0x1FFF0000) where no application code runs at all.  A one-shot
    OpenOCD session that resets, loads the flash vector by hand and resumes is
    the only reliable restart on this setup.
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


def reader(port, seconds, out_lock):
    try:
        ser = serial.Serial(port, 115200, timeout=0.2)
    except Exception as exc:  # noqa: BLE001 - report and skip this port
        with out_lock:
            print("[%s] OPEN FAILED: %s" % (port, exc), flush=True)
        return

    buf = b""
    deadline = time.time() + seconds
    try:
        while time.time() < deadline:
            chunk = ser.read(256)
            if chunk:
                buf += chunk
    finally:
        ser.close()

    text = buf.decode("utf-8", errors="replace")
    with out_lock:
        print(BANNER % port, flush=True)
        if text:
            sys.stdout.write(text)
            if not text.endswith("\n"):
                sys.stdout.write("\n")
        else:
            print("(no data)", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ports", nargs="+", default=["COM3"])
    ap.add_argument("--seconds", type=float, default=12.0)
    ap.add_argument("--no-reset", action="store_true")
    args = ap.parse_args()

    lock = threading.Lock()
    threads = [
        threading.Thread(target=reader, args=(p, args.seconds, lock), daemon=True)
        for p in args.ports
    ]
    for t in threads:
        t.start()

    time.sleep(0.8)
    if not args.no_reset:
        print("--- restarting target at flash vector ---", flush=True)
        reset_target()

    for t in threads:
        t.join()


if __name__ == "__main__":
    main()
