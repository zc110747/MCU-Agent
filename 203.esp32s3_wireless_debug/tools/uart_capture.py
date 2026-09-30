#!/usr/bin/env python3
"""Capture ESP32-S3 console output with arrival timestamps.

Usage:  python tools/uart_capture.py [PORT] [BAUD] [SECONDS] [--no-reset]

Prints each chunk with a relative timestamp so a hang can be distinguished
from missing output.
"""
import sys
import time

import serial


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM21"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0
    do_reset = "--no-reset" not in sys.argv

    ser = serial.Serial(port, baud, timeout=0.5)
    ser.setDTR(False)
    ser.setRTS(False)
    time.sleep(0.1)

    if do_reset:
        ser.setRTS(True)
        time.sleep(0.1)
        ser.setRTS(False)
        print("[%.2fs] --- reset asserted ---" % 0.0)

    ser.reset_input_buffer()
    t0 = time.time()
    total = 0
    while time.time() - t0 < seconds:
        data = ser.read(4096)
        if data:
            total += len(data)
            dt = time.time() - t0
            print("[%6.2fs] +%d bytes (total %d)" % (dt, len(data), total))
            try:
                sys.stdout.write(data.decode("utf-8", errors="replace"))
            except Exception:
                pass
            sys.stdout.flush()
    ser.close()
    print("\n[total %d bytes]" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
