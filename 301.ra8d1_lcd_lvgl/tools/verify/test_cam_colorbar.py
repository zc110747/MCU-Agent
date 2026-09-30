#!/usr/bin/env python3
"""Colorbar validation with SWD framebuffer analysis.

Sequence (pitfall-aware):
  1. Open serial FIRST (no banner loss).
  2. pyOCD connect + t.reset()  -> without reset the shell RX goes deaf.
  3. Serial: cam init -> bar on -> snap.
  4. SWD: read 153600 B framebuffer @0x680bb800 (AHB-AP, CPU running).
  5. Analyze pixel distribution + stripe periodicity (320 px wide, 8 bars
     -> 40 px period, vertical bars -> column-constant color).
  6. bar off -> snap -> sums must differ.

PASS criteria:
  - snaps OK
  - top-8 pixel values cover > 90% of the frame (few distinct colors)
  - column periodicity P(v[x,y] == v[(x+40)%320, y]) > 0.90
  - scene frame sum differs from colorbar frame sum
"""
import re
import sys
import time
from collections import Counter

import serial
from pyocd.core.helpers import ConnectHelper

FB_ADDR = 0x680BB800
FB_SIZE = 320 * 240 * 2
COM = "COM9"

s = serial.Serial(COM, 115200, timeout=0.2)

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

fails = []

# ---- 1+2. serial up, then pyOCD connect + reset --------------------------
session = ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                                  frequency=1000000,
                                                  halt_on_connect=False)
session.__enter__()
t = session.target
t.reset()                       # MANDATORY: connect without reset deafens RX
print("[probe] pyOCD connected + target reset")
time.sleep(1.5)
chat("", 1.0)                   # flush boot banner

# ---- 3. cam init + colorbar on + snap ------------------------------------
show("cam init", chat("cam init", 8.0))
show("cam bar on", chat("cam bar on", 2.0))
out = chat("cam snap", 3.0)
show("cam snap (colorbar)", out)
if "cam snap: OK" not in out:
    fails.append("colorbar snap failed")

# ---- 4. read framebuffer over SWD ----------------------------------------
bar_fb = None
if not fails:
    t0 = time.time()
    bar_fb = t.read_memory_block8(FB_ADDR, FB_SIZE)
    print(f"[probe] fb read: {len(bar_fb)} bytes in {time.time()-t0:.1f}s")

# ---- 6. bar off + scene snap ---------------------------------------------
show("cam bar off", chat("cam bar off", 2.0))
out2 = chat("cam snap", 3.0)
show("cam snap (scene)", out2)
m2 = re.search(r"cam snap: OK.*sum=0x([0-9a-f]+)", out2, re.S)
if m2:
    sum_scene = int(m2.group(1), 16)

# ---- 5. analysis ----------------------------------------------------------
sum_bar = None
if bar_fb is not None:
    sum_bar = sum(bar_fb) & 0xFFFFFFFF
    pix_le = [bar_fb[i] | (bar_fb[i + 1] << 8) for i in range(0, FB_SIZE, 2)]
    cnt = Counter(pix_le)
    top8 = cnt.most_common(8)
    cover8 = sum(c for _, c in top8) / len(pix_le)
    print(f"\ndistinct pixel values: {len(cnt)}")
    print(f"top-8 coverage: {cover8*100:.2f}%")
    for v, c in top8:
        print(f"  0x{v:04x}: {c} ({c/len(pix_le)*100:.2f}%)")

    # stripe periodicity: 8 vertical bars over 320 px -> 40 px period
    w = 320
    h = 240
    match = 0
    total = 0
    for y in range(0, h, 4):          # sample every 4th row
        row = pix_le[y * w:(y + 1) * w]
        for x in range(w):
            if row[x] == row[(x + 40) % w]:
                match += 1
            total += 1
    periodicity = match / total
    print(f"stripe periodicity (40 px): {periodicity*100:.2f}%  ({match}/{total})")

    if len(cnt) > 64:
        fails.append(f"too many distinct values ({len(cnt)}) - not a colorbar")
    if cover8 < 0.90:
        fails.append(f"top-8 coverage {cover8*100:.1f}% < 90%")
    if periodicity < 0.90:
        fails.append(f"stripe periodicity {periodicity*100:.1f}% < 90%")

if sum_bar is not None and sum_scene is not None and sum_bar == sum_scene:
    fails.append("scene sum equals colorbar sum")

session.__exit__(None, None, None)
s.close()

print()
print(f"sum(colorbar fb): {hex(sum_bar) if sum_bar is not None else 'n/a'}")
print(f"sum(scene):       {hex(sum_scene) if sum_scene is not None else 'n/a'}")
print("FAILS:", fails if fails else "none")
print(f"RESULT: {'PASS' if not fails else 'FAIL'}")
sys.exit(0 if not fails else 1)
