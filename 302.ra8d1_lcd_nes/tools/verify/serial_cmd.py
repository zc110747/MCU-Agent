#!/usr/bin/env python3
"""Serial shell helper: reset the board, then drive msh commands and capture output.

Usage:
    python tools/verify/serial_cmd.py reset
    python tools/verify/serial_cmd.py "gpu info" "gpu blit"

Prints everything the board emits, so a command's raw output can be quoted as
evidence.  Uses pyserial from the system Python (D:/software/Python3).
"""
import re
import subprocess
import sys
import threading
import time

import serial

PORT = "COM9"
BAUD = 115200

# pyOCD resets the target but leaves the ART-Link virtual COM port attached;
# reopen it after the reset so the boot log is not lost.
RE_RESET = re.compile(r"^reset$")


def open_port():
    s = serial.Serial(PORT, BAUD, timeout=0.1)
    return s


def reset_target(settle=1.5):
    subprocess.run(
        ["pyocd", "reset", "-t", "cortex_m", "-O", "connect_mode=pre-reset"],
        capture_output=True,
        timeout=90,
    )
    time.sleep(settle)


def run(commands, do_reset, listen_s):
    buf = bytearray()
    stop = threading.Event()

    def reader(s):
        while not stop.is_set():
            try:
                d = s.read(4096)
            except Exception:
                break
            if d:
                buf.extend(d)

    s = open_port()
    th = threading.Thread(target=reader, args=(s,), daemon=True)
    th.start()

    if do_reset:
        reset_target()
        time.sleep(1.0)

    for cmd in commands:
        s.write((cmd + "\r\n").encode())
        time.sleep(0.5)

    time.sleep(listen_s)
    stop.set()
    th.join(timeout=1.0)
    s.close()

    sys.stdout.write(buf.decode("utf-8", "replace"))
    return buf.decode("utf-8", "replace")


if __name__ == "__main__":
    args = sys.argv[1:]
    do_reset = False
    if args and args[0] == "reset":
        do_reset = True
        args = args[1:]
    text = run(args, do_reset, listen_s=4.0)
    sys.exit(0)
