#!/usr/bin/env python3
"""Watch the OFFICIAL camera firmware boot: does it detect the sensor?"""
import time
import serial

time.sleep(2.0)
s = serial.Serial("COM9", 115200, timeout=0.2)
out = bytearray()
t0 = time.time()
while time.time() - t0 < 12.0:
    out.extend(s.read(4096))
text = out.decode(errors="replace")
print(text[:2000])
print("=====")
print("has demo banner:", "camera display demo" in text)
print("sensor-not-found:", "not found" in text.lower())
s.close()
