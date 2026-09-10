#!/usr/bin/env python3
"""Hold the ESP32-S3 in reset for N seconds (RTS -> EN) then release.

Usage:  python tools/hold_reset.py [PORT] [HOLD_SECONDS]
"""
import sys
import time

import serial


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM21"
    hold = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0

    ser = serial.Serial(port, 115200, timeout=0.5)
    ser.setDTR(False)
    ser.setRTS(True)  # assert reset (EN low on most CH343/CP210x boards)
    print("[hold] reset asserted for %.0fs" % hold)
    sys.stdout.flush()
    time.sleep(hold)
    ser.setRTS(False)
    print("[hold] reset released")
    ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
