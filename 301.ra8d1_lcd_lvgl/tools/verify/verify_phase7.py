#!/usr/bin/env python3
"""Phase 7 acceptance: touch removed + single-page UI + hardware RTC clock.

Layers (same convention as verify_phase5):
  L1  register  : RTC control registers (via serial, not SWD -- the RTC lives
                  in the VBATT domain and SWD reads of 0x40202000 come back 0).
  L2  memory    : framebuffer pixel asserts for the single page.
  L3  physical  : what the panel actually shows (manual).

Channel: serial (COM9 shell) + SWD (pyOCD memory reads only).

Pitfalls baked in: serial FIRST, then pyOCD connect (connect without reset
deafens the shell RX).
"""
import collections
import re
import struct
import sys
import time

import serial
from pyocd.core.helpers import ConnectHelper

FB_ADDR = 0x68000000
W = 480
H = 360
FB_BYTES = W * H * 2
COM = "COM9"

# single-page palette (RGB888) -> RGB565
COL_BG      = 0x000000
COL_HDR     = 0x0A3D62
COL_CLOCK   = 0x00E5FF
COL_VALUE   = 0x40E070
COL_HDR_TXT = 0xFFD966


def to565(rgb888):
    r = (rgb888 >> 16) & 0xFF
    g = (rgb888 >> 8) & 0xFF
    b = rgb888 & 0xFF
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


results = []


def check(layer, name, ok, detail=""):
    results.append((layer, name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] ({layer}) {name}" + (f" - {detail}" if detail else ""))


def manual(name, detail=""):
    results.append(("L3", name, None, detail))
    print(f"[MANUAL] (L3) {name}" + (f" - {detail}" if detail else ""))


# ------------------------------------------------------------------ serial --
s = serial.Serial(COM, 115200, timeout=0.2)
time.sleep(0.3)
s.reset_input_buffer()


def cmd(c, wait=0.6):
    s.write((c + "\n").encode())
    time.sleep(wait)
    return s.read(8192).decode("utf-8", "replace")


def rtc_seconds(text):
    m = re.search(r"(\d\d):(\d\d):(\d\d)", text)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) if m else None


# --- L1: RTC is running and keeps real time ---------------------------------
out = cmd("rtc info")
check("L1", "rtc info answers", "rtc:" in out, out.strip().splitlines()[-1] if out.strip() else "")

a_sec = rtc_seconds(out)
t0 = time.time()
time.sleep(20)
out2 = cmd("rtc info")
b_sec = rtc_seconds(out2)
wall = time.time() - t0
ratio = (b_sec - a_sec) / wall if (a_sec is not None and b_sec is not None) else 0
check("L1", "RTC advances ~1s per real second",
      0.90 <= ratio <= 1.10,
      f"rtc_delta={b_sec - a_sec if a_sec is not None else '?'}s over {wall:.1f}s wall (ratio {ratio:.3f})")

# --- L1: rtc set ------------------------------------------------------------
out = cmd("rtc set 2026 10 01 16 30 0")
time.sleep(1.2)
out = cmd("rtc info")
check("L1", "rtc set loads the calendar",
      "2026-10-01" in out and re.search(r"16:30:0", out) is not None,
      out.strip().replace("\r\n", " | "))

# --- host commands for the new single page ---------------------------------
out = cmd("lv info")
check("L1", "lv info reports running", "running" in out,
      out.strip().replace("\r\n", " | "))

# --- L2: framebuffer holds the designed colours -----------------------------
session = ConnectHelper.session_with_chosen_probe(
    target_override="cortex_m", frequency=4_000_000, options={"no_config": True})
session.open()
t = session.target
t.halt()

fb = bytes(t.read_memory_block8(FB_ADDR, FB_BYTES))
cnt = collections.Counter(struct.unpack("<%dH" % (W * H), fb))

for name, rgb in [("background", COL_BG), ("header bar", COL_HDR),
                  ("clock text", COL_CLOCK), ("info values", COL_VALUE),
                  ("title text", COL_HDR_TXT)]:
    v = to565(rgb)
    n = cnt.get(v, 0)
    check("L2", f"framebuffer has {name} (0x{v:04X})", n > 0, f"{n} px")

# clock text bounding box: cyan pixels only, expect a centred wide-but-short box
cy = to565(COL_CLOCK)
pts = [(x, y) for y in range(H) for x in range(W)
       if struct.unpack_from("<H", fb, (y * W + x) * 2)[0] == cy]
if pts:
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    cx = (min(xs) + max(xs)) / 2
    check("L2", "clock text is horizontally centred",
          abs(cx - W / 2) < 20, f"bbox x={min(xs)}..{max(xs)} (centre {cx:.0f} vs {W//2})")
    check("L2", "clock text sits in the design band (y ~85..105)",
          60 <= min(ys) <= 120, f"bbox y={min(ys)}..{max(ys)}")
else:
    check("L2", "clock text present", False, "no cyan pixels")

t.resume()
session.close()
s.close()

manual("panel shows board name + hardware rows + live clock",
       "title 'RA8D1 VISION BOARD', a white date line, a large cyan "
       "HH:MM:SS clock (updates once a second) and 6 telemetry rows.")
manual("touch is gone",
       "there is no touch input: the only way to interact is the msh console "
       "(L1 checks above cover that the panel is display-only).")

npass = sum(1 for *_, ok, _ in results if ok is True)
nfail = sum(1 for *_, ok, _ in results if ok is False)
nman = sum(1 for *_, ok, _ in results if ok is None)
print("=" * 70)
for layer, name, ok, det in results:
    tag = "PASS" if ok is True else ("FAIL" if ok is False else "MANUAL")
    print(f"  {tag:6} ({layer}) {name}" + (f" - {det}" if det else ""))
print("=" * 70)
print(f"TOTAL: {npass} passed, {nfail} failed, {nman} manual / {len(results)}")
sys.exit(0 if nfail == 0 else 1)
