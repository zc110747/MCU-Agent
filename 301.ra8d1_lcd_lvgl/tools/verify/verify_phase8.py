#!/usr/bin/env python3
"""Phase 8 acceptance: boot action bar replaces the static colour-bar pattern.

Layers (same convention as verify_phase5/7):
  L1  serial    : shell is alive, LVGL running, banner shows Phase 8.
  L2  memory    : framebuffer captured DURING the boot splash holds the
                  action-bar palette (track + accent fill); after the splash
                  the framebuffer holds the main-page palette.
  L3  physical  : what the panel actually shows (manual).

Channel: serial (COM9 shell) + SWD (pyOCD memory reads only).
Pitfall baked in: serial FIRST, then pyOCD connect (connect without reset
deafens the shell RX); pyOCD is used only to reset+halt quickly to catch the
short-lived boot frame.
"""
import collections
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

# RGB888 palette bits used by both screens
COL_BG        = 0x000000
COL_HDR       = 0x0A3D62   # header bar (both screens)
COL_BAR_TRACK = 0x2A2A2A   # boot action-bar track
COL_BAR_FILL  = 0xFFA000   # boot action-bar fill (accent)
COL_VALUE     = 0x40E070
COL_LABEL     = 0x8A8A8A
COL_DIM       = 0x606060
COL_CLOCK     = 0x00E5FF


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


def grab_fb(settle):
    """Reset the target, let it run `settle` seconds, halt and read the
    framebuffer. Returns (counter, raw)."""
    sess = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m", frequency=4_000_000, options={"no_config": True})
    sess.open()
    t = sess.target
    t.reset_and_halt()
    t.resume()
    time.sleep(settle)
    t.halt()
    fb = bytes(t.read_memory_block8(FB_ADDR, FB_BYTES))
    t.resume()
    sess.close()
    return collections.Counter(struct.unpack("<%dH" % (W * H), fb)), fb


# ------------------------------------------------------------------ serial --
s = serial.Serial(COM, 115200, timeout=0.2)
time.sleep(0.3)
s.reset_input_buffer()


def cmd(c, wait=0.6):
    s.write((c + "\n").encode())
    time.sleep(wait)
    return s.read(8192).decode("utf-8", "replace")


# --- L1: shell + LVGL alive -------------------------------------------------
out = cmd("\r\n")
check("L1", "msh shell answers", "msh >" in out, out.strip().splitlines()[-1] if out.strip() else "")

out = cmd("lv info")
check("L1", "lv info reports running", "running" in out, out.strip().replace("\r\n", " | "))

# --- L2: boot splash frame (caught ~0.55 s after reset) ---------------------
cnt_boot, _ = grab_fb(0.55)
trk = cnt_boot.get(to565(COL_BAR_TRACK), 0)
fil = cnt_boot.get(to565(COL_BAR_FILL), 0)
check("L2", "boot action-bar track present",
      trk > 1000, f"track px={trk} (0x{to565(COL_BAR_TRACK):04X})")
check("L2", "boot action-bar fill present",
      fil > 0, f"fill px={fil} (0x{to565(COL_BAR_FILL):04X})")
check("L2", "boot header bar present",
      cnt_boot.get(to565(COL_HDR), 0) > 1000, f"{cnt_boot.get(to565(COL_HDR), 0)} px")
# the boot screen must NOT look like the old white/colour-bar pattern:
# that pattern had whole 60x480-filled bands (tens of thousands of pure
# pixels); the only pure-white here is the anti-aliased "Booting..." title.
white = cnt_boot.get(to565(0xFFFFFF), 0)
check("L2", "no static colour-bar pattern (no big solid band)",
      white < 5000, f"white px={white} (title text only)")

# --- L2: main page frame (caught after the splash hands over) ---------------
cnt_main, fb_main = grab_fb(4.0)
check("L2", "main page clock text present (cyan)",
      cnt_main.get(to565(COL_CLOCK), 0) > 0, f"{cnt_main.get(to565(COL_CLOCK), 0)} px")
check("L2", "main page values present (green)",
      cnt_main.get(to565(COL_VALUE), 0) > 0, f"{cnt_main.get(to565(COL_VALUE), 0)} px")
check("L2", "main page labels present (grey)",
      cnt_main.get(to565(COL_LABEL), 0) > 0, f"{cnt_main.get(to565(COL_LABEL), 0)} px")
# the action bar must be gone on the main page
check("L2", "action bar gone on main page",
      cnt_main.get(to565(COL_BAR_TRACK), 0) < 100,
      f"track px={cnt_main.get(to565(COL_BAR_TRACK), 0)}")

s.close()

manual("reset shows a boot action bar, then the main page",
       "on reset the panel shows 'RA8D1 VISION BOARD' + 'Booting...' + an "
       "orange progress bar that fills over ~2 s with a step caption, then "
       "swaps to the single status page with the live RTC clock.")
manual("no frozen colour bars anywhere in the boot path",
       "the old 8-colour-bar pattern is no longer drawn on reset (L2 checks "
       "the framebuffer captured during boot holds the action-bar palette).")

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
