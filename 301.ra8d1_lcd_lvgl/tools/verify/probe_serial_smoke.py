#!/usr/bin/env python3
"""Serial smoke test: list ports, listen for heartbeats, poke the shell."""
import time
from serial.tools import list_ports

print("ports:", [(p.device, p.description) for p in list_ports.comports()])

import serial
try:
    s = serial.Serial("COM9", 115200, timeout=0.1)
except Exception as e:
    print("open COM9 failed:", e)
    raise SystemExit(1)

print("listening 4 s (expect heartbeat every 2 s)...")
out = bytearray()
t0 = time.time()
while time.time() - t0 < 4.0:
    out += s.read(4096)
print("rx bytes:", len(out))
print(out.decode(errors="replace")[:400])

print("sending CR-LR pokes...")
for _ in range(3):
    s.write(b"\r\n")
    time.sleep(0.5)
out += s.read(4096)
print("total rx:", len(out))
print(out.decode(errors="replace")[-600:])
s.close()
