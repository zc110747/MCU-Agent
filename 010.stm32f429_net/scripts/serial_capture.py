#!/usr/bin/env python3
"""Capture STM32 USART1 console (COM3 @ 115200) for N seconds, print collected text.

Usage: python serial_capture.py [PORT] [SECONDS]
"""
import sys
import time
import threading
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0

s = serial.Serial(PORT, 115200, timeout=0.2)
buf = bytearray()
stop = time.time() + SECS


def reader():
    while time.time() < stop:
        try:
            n = s.in_waiting
        except Exception:
            break
        if n:
            buf.extend(s.read(n))
        else:
            time.sleep(0.02)


t = threading.Thread(target=reader, daemon=True)
t.start()
t.join()

text = bytes(buf).decode("utf-8", errors="replace")
print(text, end="")
print("\n--- captured %d bytes ---" % len(buf))
s.close()
