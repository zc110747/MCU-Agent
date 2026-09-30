#!/usr/bin/env python3
"""CEU capture test: init (if needed) -> snap x2 -> compare frame sums.

PASS criteria:
  - both snaps return OK
  - sums differ between the two snaps (live sensor data, not a frozen bus)
  - sum != 0 (framebuffer actually written)
"""
import re
import sys
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

def show(tag, out):
    print(f"--- {tag} ---")
    for line in out.splitlines():
        if line.strip() and "heartbeat" not in line:
            print("  |", line.strip())

stat = chat("cam stat", 1.5)
show("cam stat", stat)
if "cam ready" not in stat:
    init = chat("cam init", 8.0)
    show("cam init", init)
    stat = chat("cam stat", 1.5)
    show("cam stat", stat)
    if "cam ready" not in stat:
        print("RESULT: FAIL (camera not ready)")
        sys.exit(1)

sums = []
snaps_ok = True
for i in (1, 2):
    out = chat("cam snap", 3.0)
    show(f"cam snap #{i}", out)
    m = re.search(r"cam snap: OK fb=0x([0-9a-f]+) (.*) sum=0x([0-9a-f]+)", out)
    if not m:
        snaps_ok = False
        break
    sums.append(int(m.group(3), 16))

passed = snaps_ok and len(sums) == 2 and sums[0] != 0 and sums[1] != 0 and sums[0] != sums[1]
print()
print(f"snap sums: {[hex(x) for x in sums]}")
print(f"sum changed between snaps: {bool(sums) and len(sums) == 2 and sums[0] != sums[1]}")
print(f"RESULT: {'PASS' if passed else 'FAIL'}")
sys.exit(0 if passed else 1)
