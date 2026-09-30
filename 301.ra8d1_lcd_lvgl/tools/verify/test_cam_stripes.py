#!/usr/bin/env python3
"""Colorbar structure forensics: dump fb and find the real stripe geometry.

Checks (byte stream + 16-bit LE/BE views):
  - first 96 bytes hex
  - row0 run-length structure
  - row0 vs row1 / row30 / row120 equality (are rows uniform?)
  - best horizontal period k for row0 (bytes and pixels)
  - column structure: col0 down the frame
Saves fb to fb_colorbar.bin for offline inspection.
"""
import sys
import time
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

session = ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                                  frequency=1000000,
                                                  halt_on_connect=False)
session.__enter__()
t = session.target
t.reset()
print("[probe] reset done")
time.sleep(1.5)
chat("", 1.0)

chat("cam init", 8.0)
print(chat("cam bar on", 1.5).strip().splitlines()[-1] if True else "")
out = chat("cam snap", 3.0)
print([l for l in out.splitlines() if "snap" in l])
fb = bytes(t.read_memory_block8(FB_ADDR, FB_SIZE))
with open("fb_colorbar.bin", "wb") as f:
    f.write(fb)
print(f"[probe] fb saved: {len(fb)} bytes")

# ---- byte stream forensics ------------------------------------------------
print("\nfirst 96 bytes hex:")
for off in range(0, 96, 16):
    print(f"  {off:04x}: " + " ".join(f"{b:02x}" for b in fb[off:off+16]))

def rle(seq, limit=12):
    runs = []
    prev = seq[0]
    n = 1
    for v in seq[1:]:
        if v == prev:
            n += 1
        else:
            runs.append((prev, n))
            prev = v
            n = 1
            if len(runs) >= limit:
                break
    runs.append((prev, n))
    return runs

row0 = fb[:640]
print("\nrow0 byte RLE (first runs):", rle(row0))

pix0 = [row0[i] | (row0[i+1] << 8) for i in range(0, 640, 2)]
print("row0 pixel(LE) RLE:", rle(pix0))
pix0be = [(row0[i] << 8) | row0[i+1] for i in range(0, 640, 2)]
print("row0 pixel(BE) RLE:", rle(pix0be))

# best horizontal byte period for row0
best = []
for k in range(2, 321):
    m = sum(1 for x in range(640 - k) if row0[x] == row0[x + k])
    best.append((m / (640 - k), k))
best.sort(reverse=True)
print("\nrow0 top-5 byte periods:", [(f"{p*100:.0f}%", k) for p, k in best[:5]])

# row-to-row equality
r1 = fb[640:1280]
r30 = fb[29*640:29*640+640]
r120 = fb[120*640:120*640+640]
print(f"row0 == row1  : {row0 == r1}")
print(f"row0 == row29 : {row0 == r30}")
print(f"row0 == row119: {row0 == r120}")
print(f"row1 == row29 : {r1 == r30}")

# column forensics: bytes at x=0..15 down 8 rows
print("\ncolumn bytes (first 8 rows, x=0..15):")
for y in range(8):
    print(f"  row{y:3d}: " + " ".join(f"{fb[y*640+x]:02x}" for x in range(16)))

# vertical structure: same pixel x across y (LE), sample x=0..8
print("\npixel(LE) down column x=0..7, y=0..240 step 20:")
for x in range(8):
    col = [fb[(y*320+x)*2] | (fb[(y*320+x)*2+1] << 8) for y in range(0, 240, 20)]
    print(f"  x={x}: " + " ".join(f"{v:04x}" for v in col))

# restore normal mode
chat("cam bar off", 1.5)
chat("cam snap", 2.0)
session.__exit__(None, None, None)
s.close()
print("\ndone")
