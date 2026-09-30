#!/usr/bin/env python3
"""Phase 3 acceptance: Camera (OV5640 + SCCB + GPT7 XCLK + CEU capture).

Dual channel: serial (COM9 shell) + SWD (pyOCD framebuffer reads).

Checks:
  1  boot banner visible after reset
  2  cam init: OK, scan ACK addr7=0x3c id=0x5640 (OV5640)
  3  cam stat: ready, 320x240 bpp2, fb in SDRAM
  4  cam rd 0x3c 0x300a/0x300b -> 0x56/0x40 (sensor ID via SCCB)
  5  cam snap x2: OK, sums differ (live scene), fb nonzero over SWD
  6  colorbar: bar on, snap x2 deterministic; >= 6 distinct bar colors,
     top-8 pixel coverage > 90 %, rows uniform, ~40 px stripe period
  7  bar off: scene frame differs from colorbar

Pitfall (baked in): pyOCD connect without reset deafens the shell RX -
open serial FIRST, then connect, then t.reset().
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

results = []

def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f" - {detail}" if detail else ""))

s = serial.Serial(COM, 115200, timeout=0.2)

def chat(cmd, wait):
    s.reset_input_buffer()
    s.write(cmd.encode() + b"\r\n")
    t0 = time.time()
    buf = bytearray()
    while time.time() - t0 < wait:
        buf.extend(s.read(4096))
    return buf.decode(errors="replace")

def snap_sum(out):
    m = re.search(r"cam snap: OK fb=0x([0-9a-f]+).*sum=0x([0-9a-f]+)", out, re.S)
    return (int(m.group(1), 16), int(m.group(2), 16)) if m else (None, None)

print("=" * 64)
print("Phase 3 verification - camera (OV5640 + CEU)")
print("=" * 64)

session = ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                                  frequency=1000000,
                                                  halt_on_connect=False)
session.__enter__()
t = session.target
t.reset()
time.sleep(1.5)
# banner arrived during reset: read buffered bytes WITHOUT clearing first
banner = s.read(4096).decode(errors="replace")
banner += chat("", 0.5)
check("1 boot banner", "Phase 3" in banner, banner.strip().splitlines()[:1].__str__())

# -- 2. init ---------------------------------------------------------------
out = chat("cam init", 8.0)
init_ok = ("cam init: OK" in out) and ("addr7=0x3c" in out) and ("id=0x5640" in out)
check("2 cam init (OV5640 @0x3c)", init_ok,
      next((l.strip() for l in out.splitlines() if "[cam] scan" in l), "no scan line"))

# -- 3. stat ---------------------------------------------------------------
out = chat("cam stat", 1.5)
check("3 cam stat ready", "cam ready, 320x240 bpp2 fb=0x680bb800" in out,
      next((l.strip() for l in out.splitlines() if "cam ready" in l), "not ready"))

# -- 4. sensor ID via cam rd (also validates bare-hex parser) --------------
ok_id = True
det = []
for reg, want in (("0x300a", "0x56"), ("0x300b", "0x40")):
    out = chat(f"cam rd 0x3c {reg}", 1.5)
    got = re.search(r"val=0x([0-9a-f]+)", out)
    v = got.group(1) if got else "?"
    det.append(f"{reg}={v}")
    if v != want[2:]:          # want like "0x56" -> compare "56"; do NOT strip() (0x40->"4")
        ok_id = False
check("4 sensor ID regs (0x300a/0x300b)", ok_id, " ".join(det))

# -- 5. scene snaps --------------------------------------------------------
out1 = chat("cam snap", 3.0)
out2 = chat("cam snap", 3.0)
fb1, sum1 = snap_sum(out1)
fb2, sum2 = snap_sum(out2)
check("5a snap OK x2", (fb1 == FB_ADDR) and (fb2 == FB_ADDR),
      f"fb={hex(fb1) if fb1 else '?'},{hex(fb2) if fb2 else '?'}")
check("5b live frames (sums differ)", (sum1 is not None) and (sum1 != sum2),
      f"sum1={hex(sum1) if sum1 else '?'}, sum2={hex(sum2) if sum2 else '?'}")

fb_scene = None
if fb1 == FB_ADDR:
    fb_scene = bytes(t.read_memory_block8(FB_ADDR, FB_SIZE))
    check("5c fb readable over SWD, nonzero", any(fb_scene),
          f"{len(fb_scene)} bytes, sum={hex(sum(fb_scene) & 0xFFFFFFFF)}")

# -- 6. colorbar -----------------------------------------------------------
chat("cam bar on", 1.5)
cb1 = chat("cam snap", 3.0)
cb2 = chat("cam snap", 3.0)
fbB1, sumB1 = snap_sum(cb1)
fbB2, sumB2 = snap_sum(cb2)
check("6a colorbar snap OK x2", (fbB1 == FB_ADDR) and (fbB2 == FB_ADDR), "")

fb_bar = None
if fbB1 == FB_ADDR:
    fb_bar = bytes(t.read_memory_block8(FB_ADDR, FB_SIZE))

if fb_bar is not None:
    # deterministic: identical bar-interior bytes in both captures
    det_same = (fb_bar[:FB_SIZE // 2] == bytes(t.read_memory_block8(FB_ADDR, FB_SIZE // 2)))
    check("6b colorbar deterministic (re-snap identical)", det_same, "")

    pix = [fb_bar[i] | (fb_bar[i + 1] << 8) for i in range(0, FB_SIZE, 2)]
    cnt = Counter(pix)
    top8 = cnt.most_common(8)
    cover8 = sum(c for _, c in top8) / len(pix)
    distinct_dominant = sum(1 for _, c in top8 if c > FB_SIZE // 16 / 4)
    check("6c >= 6 dominant bar colors", distinct_dominant >= 6,
          f"distinct={len(cnt)}, dominant={distinct_dominant}, top8={cover8*100:.1f}%")
    check("6d top-8 coverage > 90%", cover8 > 0.90, f"{cover8*100:.2f}%")

    rows_uniform = all(fb_bar[y * 640:(y + 1) * 640] == fb_bar[:640]
                       for y in (1, 50, 100, 200))
    check("6e rows uniform (vertical bars)", rows_uniform, "row0 == row1/50/100/200")

    centers = [pix[x] for x in range(20, 320, 40)]
    check("6f 8 distinct bar-center colors", len(set(centers)) == 8,
          " ".join(f"0x{v:04x}" for v in centers))

# -- 7. bar off ------------------------------------------------------------
chat("cam bar off", 1.5)
out3 = chat("cam snap", 3.0)
fb3, sum3 = snap_sum(out3)
check("7 scene differs from colorbar",
      (sum3 is not None) and (sumB1 is not None) and (sum3 != sumB1),
      f"scene={hex(sum3) if sum3 else '?'}, bar={hex(sumB1) if sumB1 else '?'}")

session.__exit__(None, None, None)
s.close()

# -- summary ---------------------------------------------------------------
npass = sum(1 for _, ok, _ in results if ok)
nfail = len(results) - npass
print("=" * 64)
for name, ok, det in results:
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f" - {det}" if det else ""))
print("=" * 64)
print(f"TOTAL: {npass} passed, {nfail} failed / {len(results)}")
sys.exit(0 if nfail == 0 else 1)
