#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Diagnostic: listen on COM7 while resetting the board to force a boot banner,
then send 'p' to confirm the command echo path. Hexdumps raw bytes."""
import subprocess
import sys
import threading
import time

import serial

PORT = "COM7"
BAUD = 115200


def main():
    print("[DIAG] opening %s @%d" % (PORT, BAUD))
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.2)
    except Exception as e:
        print("[DIAG] serial open FAILED: %s" % e)
        return 2
    time.sleep(0.3)
    ser.reset_input_buffer()

    buf = bytearray()

    def reader():
        while True:
            try:
                b = ser.read(ser.in_waiting or 1)
            except Exception:
                break
            if b:
                buf.extend(b)
                sys.stdout.write("[RX] " + b.decode(errors="replace"))
                sys.stdout.flush()

    threading.Thread(target=reader, daemon=True).start()

    time.sleep(3.0)
    print("\n[DIAG] resetting board via openocd (reset run) ...", flush=True)
    try:
        subprocess.run(
            ["openocd", "-f", "openocd.cfg", "-c", "reset_config none",
             "-c", "adapter speed 1000", "-c", "init", "-c", "reset run",
             "-c", "shutdown"],
            capture_output=True, text=True, timeout=60)
    except Exception as e:
        print("[DIAG] reset failed: %s" % e)

    time.sleep(8.0)
    print("\n[DIAG] sending 'p' ...", flush=True)
    ser.write(b"p")
    ser.flush()
    time.sleep(5.0)

    print("\n[DIAG] total bytes received: %d" % len(buf))
    print("[DIAG] hexdump(first 160): " + buf[:160].hex())
    try:
        ser.close()
    except Exception:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
