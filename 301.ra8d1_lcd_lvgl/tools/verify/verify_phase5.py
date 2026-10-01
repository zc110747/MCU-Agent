#!/usr/bin/env python3
"""Phase 5 acceptance: MIPI DSI display path (480x360) + LVGL DIRECT + menu UI.

WHY THIS SCRIPT LOOKS DIFFERENT FROM verify_phase3/4
----------------------------------------------------
Phases 2-4 accepted the display by asserting framebuffer *contents* over SWD.
That only ever proved the SDRAM was written correctly - it said nothing about
whether the panel was lit.  The panel in fact never showed anything, because
the firmware was driving a parallel RGB666 interface at a MIPI DSI screen.
Those green checks were a false pass.

Phase 5 therefore splits acceptance into three layers, and the script reports
which layer each check belongs to:

  L1  register  : GLCDC + DSI/PHY register state.  Proves the controller and
                  the serial link are configured and running.
  L2  memory    : framebuffer pixel asserts.  Proves the render pipeline
                  produces the right image in memory.
  L3  physical  : whether photons actually leave the panel.  This CANNOT be
                  checked over SWD - it is printed as a manual step.

Channel: serial (COM9 shell) + SWD (pyOCD memory reads only).

Pitfalls baked in: serial FIRST, then pyOCD connect, then t.reset() (connect
without reset deafens the shell RX).
"""
import re
import sys
import time

import serial
from pyocd.core.helpers import ConnectHelper

# Two DIRECT-mode pages, page 0 first (that is what R_GLCDC_Open() points at).
FB_ADDR = 0x68000000
W = 480
H = 360
FB_BYTES = W * H * 2          # 345,600
COM = "COM9"

results = []


def check(layer, name, ok, detail=""):
    results.append((layer, name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] ({layer}) {name}" + (f" - {detail}" if detail else ""))


def manual(name, detail=""):
    results.append(("L3", name, None, detail))
    print(f"[MANUAL] (L3) {name}" + (f" - {detail}" if detail else ""))


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


print("=" * 70)
print("Phase 5 verification - MIPI DSI 480x360 + LVGL DIRECT")
print("=" * 70)

session = ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                                  frequency=1000000,
                                                  halt_on_connect=False)
session.__enter__()
t = session.target
t.reset()
time.sleep(1.5)

banner = s.read(8192).decode(errors="replace") + chat("", 0.5)
check("L2", "1 boot banner (Phase 5)", "Phase 5" in banner,
      next((l.strip() for l in banner.splitlines() if "Vision Board" in l), "?"))

# ---------------------------------------------------------------- L1 registers
time.sleep(2.0)   # GLCDC up + DCS table + first LVGL renders

stat = chat("lcd stat", 1.0)
m = re.search(r"BG\.EN=0x([0-9a-f]{8}) \(EN=(\d) VEN=(\d)\)", stat)
check("L1", "2 GLCDC BG.EN (EN=1 VEN=0)", bool(m) and m.group(2) == "1" and m.group(3) == "0",
      f"BG.EN=0x{m.group(1)}" if m else "no match")

# NOTE: BG.HSIZE / BG.VSIZE cannot be asserted for equality with 480/360.
# Those registers pack HW[10:0]+HP[26:16] (and VW+VP) into one word, and while
# VEN==0 the written values are not reflected back onto the bus - reading them
# yields a garbage-looking packed word.  This was verified on silicon by
# probing R_GLCDC directly over SWD.  The authoritative geometry check is the
# framebuffer size + the PANEL_CLK / DSI timing, so we assert PANEL_CLK here
# and leave the pixel-count assertions to L2.
m2 = re.search(r"PANEL_CLK=0x([0-9a-f]{8}) \(DCDR=(\d+) CLKEN=(\d) CLKSEL=(\d)\)", stat)
check("L1", "3 GLCDC panel clock enabled (CLKEN=1)",
      bool(m2) and m2.group(3) == "1",
      f"PANEL_CLK=0x{m2.group(1)} DCDR={m2.group(2)} CLKSEL={m2.group(4)}" if m2 else "no match")

m3 = re.search(r"STMON=0x([0-9a-f]{8}) \(L1UNDF=(\d) L2UNDF=(\d)\)", stat)
# L2UNDF is a documented false positive: layer 2 is unused and underflows
# permanently.  Only layer 1 (the one that actually fetches) is meaningful.
check("L1", "4 no layer-1 underflow (L1UNDF=0)", bool(m3) and m3.group(2) == "0",
      f"STMON=0x{m3.group(1)} L2UNDF={m3.group(3)} (unused layer, expected 1)"
      if m3 else "no match")

dsi = chat("lcd dsi", 1.5)
mc = re.search(r"DSI cmds=(\d+) seq0=(\d+) phy_status=0x([0-9a-f]+)", dsi)
# seq0 is the number of DCS commands whose SEQ0 completion interrupt fired.
# It must track cmds closely - a large gap means the SEQ0 vector is not being
# serviced (which is exactly what a missing R_MIPI_DSI_Open() looks like).
seq0_ok = bool(mc) and int(mc.group(1)) >= 40 and int(mc.group(2)) >= int(mc.group(1)) - 2
check("L1", "5 DCS command table pushed + SEQ0 completions",
      seq0_ok,
      f"{mc.group(1)} commands, seq0={mc.group(2)}, phy_status=0x{mc.group(3)}"
      if mc else "no match")

ml = re.search(r"DSI link_status=0x([0-9a-f]+) \(CH0=(\d) CH1=(\d) VIDEO=(\d)\)", dsi)
check("L1", "6 DSI link running (video mode)", bool(ml) and ml.group(4) == "1",
      f"link_status=0x{ml.group(1)} CH0={ml.group(2)} CH1={ml.group(3)} VIDEO={ml.group(4)}"
      if ml else "no match")

ma = re.search(r"DSI ack_err=0x([0-9a-f]{8})", dsi)
check("L1", "7 no accumulated DSI ack/err", bool(ma) and int(ma.group(1), 16) == 0,
      f"ack_err=0x{ma.group(1)}" if ma else "no match")

mv = re.search(r"vsync=(\d+)", dsi)
vs1 = int(mv.group(1)) if mv else 0
time.sleep(1.0)
vs2 = int(re.search(r"count=(\d+)", chat("lcd vsync", 0.8)).group(1))
check("L1", "8 vsync advancing (~45 Hz)", vs2 > vs1 + 20, f"{vs1} -> {vs2} in 1 s")

# ------------------------------------------------------------------ L2 memory
# In DIRECT mode a *static* page produces no dirty area, so flush_cb is not
# called and `fps` legitimately reads low.  What proves the LVGL thread is
# alive is the loop/handler counter, which advances every lap regardless.
info = lv_info()
check("L2", "9 lv running", bool(info) and info[0] == "running", str(info))

loop1 = re.search(r"lv loop=(\d+) handler=(\d+)", chat("lv info", 1.2))
l1 = int(loop1.group(1)) if loop1 else -1
time.sleep(1.0)
loop2 = re.search(r"lv loop=(\d+) handler=(\d+)", chat("lv info", 1.2))
l2 = int(loop2.group(1)) if loop2 else -1
check("L2", "10 LVGL thread alive (loop counter advancing)", l2 > l1,
      f"loop {l1} -> {l2} in 1 s")

# The info page refreshes its DSI/LVGL/Memory/VSync rows from a 1 Hz timer,
# so the framebuffer genuinely changes over a ~2 s window.  Drive it to the
# info page first, otherwise a static menu page would legitimately not change.
chat("menu select 3", 0.5)
time.sleep(0.3)
chat("menu enter", 0.5)
time.sleep(1.5)
fb1 = bytes(t.read_memory_block8(FB_ADDR, FB_BYTES))
time.sleep(2.2)
fb2 = bytes(t.read_memory_block8(FB_ADDR, FB_BYTES))
check("L2", "11 info page refreshes (fb changes over 2 s)", fb1 != fb2,
      f"sum {sum(fb1) & 0xFFFFFFFF:08x} vs {sum(fb2) & 0xFFFFFFFF:08x}")
chat("menu back", 0.6)
time.sleep(0.8)

# Deterministic test screen.  Four 100x60 rects at x = 10/130/250/370, y = 10.
chat("lv test", 1.0)
time.sleep(0.8)
fbt = bytes(t.read_memory_block8(FB_ADDR, FB_BYTES))

expect = [((60, 40), 0xF800), ((180, 40), 0x07E0),
          ((300, 40), 0x001F), ((420, 40), 0xFFFF)]
ok_all = True
det = []
for (x, y), want in expect:
    got = px(fbt, x, y)
    ok = (got == want)
    ok_all = ok_all and ok
    det.append(f"({x},{y})=0x{got:04x}{'' if ok else '!=0x%04x' % want}")
check("L2", "12 solid rects exact colors", ok_all, " ".join(det))

bg = px(fbt, 460, 300)
check("L2", "13 test screen bg black", bg == 0x0000, f"(460,300)=0x{bg:04x}")

# lv demo returns to the menu screen; assert the LVGL thread is still live
# (loop counter) rather than fps - a static page does not redraw in DIRECT mode.
chat("lv demo", 1.0)
time.sleep(1.5)
loop3 = re.search(r"lv loop=(\d+) handler=(\d+)", chat("lv info", 1.2))
l3 = int(loop3.group(1)) if loop3 else -1
time.sleep(1.0)
loop4 = re.search(r"lv loop=(\d+) handler=(\d+)", chat("lv info", 1.2))
l4 = int(loop4.group(1)) if loop4 else -1
check("L2", "14 lv demo -> menu, LVGL thread still live", l4 > l3,
      f"loop {l3} -> {l4} in 1 s")

# ------------------------------------------------------------- menu (msh) ----
# The board has no buttons, so the menu cursor is driven over the console.
# Requests are posted from the msh thread and served on the LVGL thread.
mn = chat("menu list", 1.2)
mm = re.search(r"menu page=(\d+) action=(\d+)", mn)
check("L2", "15 menu page is live (page=1, boot gate passed)",
      bool(mm) and mm.group(1) == "1",
      f"page={mm.group(1)} action={mm.group(2)}" if mm else "no match")

rows = re.findall(r"menu \[(\d)\] ", mn)
check("L2", "16 menu has 4 rows", len(rows) == 4, f"rows={len(rows)}")

# Walk the cursor relative to wherever it is now (earlier checks may have
# moved it).  Go to row 0 first, then down twice must land on index 2.
for _ in range(6):
    chat("menu up", 0.15)
time.sleep(0.4)
chat("menu down", 0.4)
time.sleep(0.3)
chat("menu down", 0.4)
time.sleep(0.3)
mn2 = chat("menu list", 1.0)
mi = re.search(r"menu page=\d+ action=\d+", mn2)
cur = re.search(r"menu \[(\d)\] \*", mn2)
check("L2", "17 cursor moves down (index 2)", bool(cur) and cur.group(1) == "2",
      f"selected index={cur.group(1) if cur else '?'}")

# Clamp test: 10 ups from index 2 must stop at 0, not wrap.
for _ in range(10):
    chat("menu up", 0.15)
time.sleep(0.4)
cur = re.search(r"menu \[(\d)\] \*", chat("menu list", 1.0))
check("L2", "18 cursor clamps at index 0", bool(cur) and cur.group(1) == "0",
      f"selected index={cur.group(1) if cur else '?'}")

# Enter on "System info" (row 3) must switch to the info page.
chat("menu select 3", 0.5)
time.sleep(0.3)
chat("menu enter", 0.5)
time.sleep(1.0)
mn3 = chat("menu list", 1.0)
mp = re.search(r"menu page=(\d+) action=(\d+)", mn3)
check("L2", "19 enter -> System info page (page=2, action=4)",
      bool(mp) and mp.group(1) == "2" and mp.group(2) == "4",
      f"page={mp.group(1)} action={mp.group(2)}" if mp else "no match")

# Back to the menu.
chat("menu back", 0.5)
time.sleep(0.8)
mn4 = chat("menu list", 1.0)
mp = re.search(r"menu page=(\d+)", mn4)
check("L2", "20 back -> menu page (page=1)", bool(mp) and mp.group(1) == "1",
      f"page={mp.group(1)}" if mp else "no match")

# ---------------------------------------------------------------- L3 physical
manual("panel actually shows the image",
       "run `lcd pattern 0` -> pure black, `lcd pattern 1` -> pure white, "
       "`lcd pattern 2` -> 8 colour bars (red/orange/yellow/green/cyan/blue/"
       "purple/white, left to right).  CONFIRMED 2026-10-01 on the official "
       "2.0\" MIPI panel after fixing GLCDC_CFG_USING_DSI.")
manual("menu UI is legible",
       "the panel should show the menu header + 4 rows with a highlighted "
       "cursor row; `menu down` moves the cursor.  CONFIRMED 2026-10-01.")
manual("system info page refreshes",
       "`menu select 3` + `menu enter` shows CPU/Display/DSI/LVGL/Memory/VSync "
       "rows updating once per second.  CONFIRMED 2026-10-01.")

session.__exit__(None, None, None)
s.close()

npass = sum(1 for _, _, ok, _ in results if ok is True)
nfail = sum(1 for _, _, ok, _ in results if ok is False)
nman = sum(1 for _, _, ok, _ in results if ok is None)
print("=" * 70)
for layer, name, ok, det in results:
    tag = "PASS" if ok is True else ("FAIL" if ok is False else "MANUAL")
    print(f"  {tag:6} ({layer}) {name}" + (f" - {det}" if det else ""))
print("=" * 70)
print(f"TOTAL: {npass} passed, {nfail} failed, {nman} manual / {len(results)}")
sys.exit(0 if nfail == 0 else 1)
