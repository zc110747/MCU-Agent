#!/usr/bin/env python3
"""Phase 4 acceptance: LVGL v9.1.0 on GLCDC RGB565 framebuffer.

Dual channel: serial (COM9 shell) + SWD (pyOCD framebuffer pixel reads).

Checks:
  1  boot banner shows Phase 4
  2  lv info: running
  3  animation liveness: fps > 20
  4  framebuffer content changes between two SWD reads (animating)
  5  lv test: 4 solid rects land exact RGB565 colors at known fb pixels
     (60,40)=0xF800 (170,40)=0x07E0 (280,40)=0x001F (390,40)=0xFFFF
  6  test screen background is black at (700,240)
  7  lv demo: animation resumes (fps > 20 again)

Pitfalls (baked in): serial FIRST, then pyOCD connect, then t.reset()
(connect without reset deafens the shell RX).
"""
import re
import sys
import time

import serial
from pyocd.core.helpers import ConnectHelper

FB_ADDR = 0x68000000          # LCD framebuffer (.sdram), 800x480 RGB565
W = 800
COM = "COM9"

results = []

def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f" - {detail}" if detail else ""))

def px(fb_bytes, x, y):
    off = (y * W + x) * 2
    return fb_bytes[off] | (fb_bytes[off + 1] << 8)

s = serial.Serial(COM, 115200, timeout=0.2)

def chat(cmd, wait):
    s.reset_input_buffer()
    s.write(cmd.encode() + b"\r\n")
    t0 = time.time()
    buf = bytearray()
    while time.time() - t0 < wait:
        buf.extend(s.read(4096))
    return buf.decode(errors="replace")

def lv_info():
    out = chat("lv info", 1.5)
    m = re.search(r"lv info: (\w+) flushes=(\d+) fps=(\d+) mem=(\d+)/(\d+)KB \((\d+)%\)", out)
    return m.groups() if m else None

print("=" * 64)
print("Phase 4 verification - LVGL v9.1.0")
print("=" * 64)

session = ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                                  frequency=1000000,
                                                  halt_on_connect=False)
session.__enter__()
t = session.target
t.reset()
time.sleep(1.5)
banner = s.read(4096).decode(errors="replace") + chat("", 0.5)
check("1 boot banner (Phase 4)", "Phase 4" in banner,
      next((l.strip() for l in banner.splitlines() if "Vision Board" in l), "?"))

time.sleep(2.0)   # let the LVGL thread finish init + first renders

info = lv_info()
check("2 lv running", bool(info) and info[0] == "running", str(info))
fps = int(info[2]) if info else 0
check("3 animation liveness (fps > 20)", fps > 20, f"fps={fps}")

fb1 = bytes(t.read_memory_block8(FB_ADDR, W * 480 * 2))
time.sleep(0.4)
fb2 = bytes(t.read_memory_block8(FB_ADDR, W * 480 * 2))
check("4 fb animating (sums differ)", fb1 != fb2,
      f"{sum(fb1) & 0xFFFFFFFF:08x} vs {sum(fb2) & 0xFFFFFFFF:08x}")

# -- deterministic test screen ---------------------------------------------
chat("lv test", 1.0)
time.sleep(0.6)   # mode timer (50 ms) + render (33 ms)
fbt = bytes(t.read_memory_block8(FB_ADDR, W * 480 * 2))

expect = [((60, 40), 0xF800), ((170, 40), 0x07E0),
          ((280, 40), 0x001F), ((390, 40), 0xFFFF)]
ok_all = True
det = []
for (x, y), want in expect:
    got = px(fbt, x, y)
    ok = (got == want)
    ok_all = ok_all and ok
    det.append(f"({x},{y})=0x{got:04x}{'' if ok else '!=0x%04x' % want}")
check("5 solid rects exact colors", ok_all, " ".join(det))

bg = px(fbt, 700, 240)
check("6 test screen bg black", bg == 0x0000, f"(700,240)=0x{bg:04x}")

chat("lv demo", 1.0)
time.sleep(1.5)
info = lv_info()
fps = int(info[2]) if info else 0
check("7 demo animating again (fps > 20)", fps > 20, f"fps={fps}, mem={info[4]}KB/{info[5]}KB" if info else str(info))

session.__exit__(None, None, None)
s.close()

npass = sum(1 for _, ok, _ in results if ok)
nfail = len(results) - npass
print("=" * 64)
for name, ok, det in results:
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f" - {det}" if det else ""))
print("=" * 64)
print(f"TOTAL: {npass} passed, {nfail} failed / {len(results)}")
sys.exit(0 if nfail == 0 else 1)
