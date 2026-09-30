#!/usr/bin/env python3
"""Post-fix test: cam init (official power sequence) + cam scan."""
import time
import serial

time.sleep(2.5)  # let the firmware boot after flash reset
s = serial.Serial("COM9", 115200, timeout=0.2)
out = bytearray()

def chat(cmd, wait):
    s.reset_input_buffer()
    s.write(cmd.encode() + b"\r\n")
    t0 = time.time()
    while time.time() - t0 < wait:
        out.extend(s.read(4096))
    return out.decode(errors="replace")

print("poke:", repr(chat("", 1.0))[:120])
print("cam init:")
for line in chat("cam init", 6.0).splitlines():
    if "cam" in line or "scan" in line:
        print("  |", line.strip())
print("cam scan:")
for line in chat("cam scan", 4.0).splitlines():
    if "cam" in line:
        print("  |", line.strip())
s.close()
