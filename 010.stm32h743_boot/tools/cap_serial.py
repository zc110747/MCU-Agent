#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Capture COM19 (ST-Link VCP) to a log file for ~40s."""
import serial, sys, time, os

PORT = "COM19"
OUT  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "serial_capture.log")
DUR  = 40.0

def main():
    try:
        ser = serial.Serial(PORT, 115200, timeout=0.2)
    except Exception as e:
        print("open %s failed: %s" % (PORT, e)); sys.exit(1)
    t0 = time.time()
    with open(OUT, "w", encoding="utf-8", errors="replace") as f:
        f.write("=== serial capture %s @115200 ===\n" % PORT)
        while time.time() - t0 < DUR:
            try:
                data = ser.read(200)
            except Exception as e:
                f.write("[read err] %s\n" % e); break
            if data:
                txt = data.decode("utf-8", "replace")
                f.write(txt)
                sys.stdout.write(txt); sys.stdout.flush()
    ser.close()
    print("\n[cap] wrote", OUT)

if __name__ == "__main__":
    main()
