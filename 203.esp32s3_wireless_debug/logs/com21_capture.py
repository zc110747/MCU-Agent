#!/usr/bin/env python3
"""Capture ESP32-S3 CMSIS-DAP probe UART0 (COM21) log for OpenOCD debugging.

Usage:
    python com21_capture.py [PORT] [BAUD] [DURATION_S] [OUTFILE]
"""
import serial
import sys
import time

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM21"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
DURATION = int(sys.argv[3]) if len(sys.argv) > 3 else 50
OUT = sys.argv[4] if len(sys.argv) > 4 else "logs/com21_capture.log"

ser = serial.Serial(PORT, BAUD, timeout=0.2)
t0 = time.time()
with open(OUT, "w", encoding="utf-8", errors="replace") as f:
    f.write("=== COM21 capture start %s dur=%ds ===\n" % (time.strftime("%H:%M:%S"), DURATION))
    f.flush()
    while time.time() - t0 < DURATION:
        try:
            n = ser.in_waiting or 1
            b = ser.read(n)
        except Exception as e:  # noqa
            f.write("[read err %s]\n" % e)
            f.flush()
            break
        if b:
            f.write(b.decode("utf-8", errors="replace"))
            f.flush()
    f.write("=== COM21 capture end %s ===\n" % time.strftime("%H:%M:%S"))
ser.close()
print("capture done ->", OUT)
