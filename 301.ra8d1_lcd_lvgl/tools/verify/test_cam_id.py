#!/usr/bin/env python3
"""Post-XCLK-fix: scan + raw register reads on the detected sensor."""
import time
import serial

time.sleep(2.5)
s = serial.Serial("COM9", 115200, timeout=0.2)

def chat(cmd, wait):
    s.reset_input_buffer()
    s.write(cmd.encode() + b"\r\n")
    t0 = time.time()
    buf = bytearray()
    while time.time() - t0 < wait:
        buf.extend(s.read(4096))
    return buf.decode(errors="replace")

for cmd, wait in (("cam scan", 3.0),
                  ("cam rd 3c 300a", 2.0),
                  ("cam rd 3c 300b", 2.0),
                  ("cam init", 8.0),
                  ("cam stat", 1.5)):
    print(f"--- {cmd} ---")
    for line in chat(cmd, wait).splitlines():
        if line.strip() and "heartbeat" not in line:
            print("  |", line.strip())
s.close()
