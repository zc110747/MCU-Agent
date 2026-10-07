#!/usr/bin/env python3
"""Independent check that the DRW2D blit actually wrote pixels to SDRAM.

Why this exists
---------------
The 'gpu blit' msh command reports a blit counter.  A counter only proves the
command list was submitted to the engine -- it does not prove the engine wrote
the framebuffer.  This script reads the GLCDC framebuffer back over SWD and
checks the gradient landed where expected while the border stayed black.

Evidence tier: reproducible measurement (SWD read-back), not inference.

Design notes
------------
* One pyocd session for all reads, driven by a `-x` commander script.
  Opening a fresh pyocd session per address makes the ART-Link probe enumerate
  unreliably and is far slower.
* `read32 <addr> <count>` takes a BYTE count, not a word count.
* Halting the core makes the RT-Thread shell go deaf, so the blit is triggered
  over the serial port *before* pyocd attaches.

Framebuffer: 0x68000000, 480x360 RGB565, stride 480 px.
Blit: 240x240 gradient centred at (120, 60):
    r = x*31/239, g = y*63/239, b = (x+y)*31/478   (x,y local to the blit)

Usage:
    python tools/verify/verify_gpu_blit.py --blit    # trigger then verify
    python tools/verify/verify_gpu_blit.py           # verify only
"""
import os
import subprocess
import sys
import tempfile
import time

import serial

PORT = "COM9"
BAUD = 115200

FB_BASE = 0x68000000
FB_STRIDE = 480

BLIT_X = 120
BLIT_Y = 60
BLIT_W = 240
BLIT_H = 240


def send_serial(cmd, settle=1.2):
    s = serial.Serial(PORT, BAUD, timeout=0.2)
    time.sleep(0.2)
    s.write((cmd + "\r\n").encode())
    time.sleep(settle)
    s.close()


def read_many(addrs):
    """Read one 32-bit word from each address, in a single pyocd session.

    Returns {addr: value}; failed reads are absent from the dict.
    """
    if not addrs:
        return {}

    uniq = list(dict.fromkeys(addrs))
    script = "\n".join(f"read32 {hex(a)} 4" for a in uniq) + "\n"

    fd, path = tempfile.mkstemp(suffix=".pyocd_cmds", text=True)
    try:
        with os.fdopen(fd, "w") as f:
            f.write(script)

        r = subprocess.run(
            ["pyocd", "cmd", "-t", "cortex_m", "-O", "connect_mode=pre-reset", "-x", path],
            capture_output=True, text=True, timeout=300,
        )

        out = {}
        for line in r.stdout.splitlines():
            if ":" not in line:
                continue
            addr_s, rhs = line.split(":", 1)
            rhs = rhs.split("|", 1)[0]
            toks = rhs.split()
            if not toks:
                continue
            try:
                out[int(addr_s.strip(), 16)] = int(toks[0], 16)
            except ValueError:
                continue
        return out
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def px_addr(x, y):
    return FB_BASE + 2 * (FB_STRIDE * y + x)


def rgb565(p):
    return ((p >> 11) & 0x1F, (p >> 5) & 0x3F, p & 0x1F)


def main():
    if "--blit" in sys.argv:
        send_serial("gpu blit")
        time.sleep(1.0)

    # (label, x, y) - absolute framebuffer coordinates.
    points = [
        ("border_tl", 0, 0),
        ("border_br", 479, 359),
        ("border_topmid", 240, 0),          # above the blit
        ("blit_tl", BLIT_X, BLIT_Y),
        ("blit_tr", BLIT_X + BLIT_W - 1, BLIT_Y),
        ("blit_bl", BLIT_X, BLIT_Y + BLIT_H - 1),
        ("blit_mid", BLIT_X + 80, BLIT_Y),
    ]
    ramp_x = [0, 60, 120, 180, 239]
    for i, dx in enumerate(ramp_x):
        points.append((f"ramp{i}", BLIT_X + dx, BLIT_Y))

    table = read_many([px_addr(x, y) for (_, x, y) in points])

    if not table:
        print("\n  [FAIL] SWD read-back returned nothing (probe enumeration?)")
        print("\n0/6 checks passed")
        return 1

    def px(label):
        for (lbl, x, y) in points:
            if lbl == label:
                v = table.get(px_addr(x, y))
                return None if v is None else (v & 0xFFFF)
        return None

    checks = []

    b = px("border_tl")
    checks.append(("border (0,0) black", b == 0x0000, f"0x{b:04x}" if b is not None else "read failed"))

    b = px("border_br")
    checks.append(("border (479,359) black", b == 0x0000, f"0x{b:04x}" if b is not None else "read failed"))

    b = px("border_topmid")
    checks.append(("border (240,0) black", b == 0x0000, f"0x{b:04x}" if b is not None else "read failed"))

    # The gradient starts at (r,g,b)=(0,0,0) because local x=y=0, so the top-left
    # corner of the blit is legitimately black keyed by the formula.  The real
    # "did anything land" evidence is the ramp and the max-valued corner; this
    # check pins the formula's origin.
    tl = px("blit_tl")
    checks.append(("blit origin (120,60) = 0x0000 (r=g=b=0)",
                   tl == 0x0000, f"0x{tl:04x}" if tl is not None else "read failed"))

    # A point one third along the top row must be non-black (r=10, g=0).
    mid = px("blit_mid")
    r_mid = rgb565(mid)[0] if mid is not None else -1
    checks.append(("blit (200,60) non-black, r=10", r_mid == 10,
                   f"r={r_mid} px=0x{mid:04x}" if mid is not None else "read failed"))

    tr = px("blit_tr")
    r_tr = rgb565(tr)[0] if tr is not None else -1
    checks.append(("top-right red field = 31", r_tr == 31,
                   f"r={r_tr} px=0x{tr:04x}" if tr is not None else "read failed"))

    bl = px("blit_bl")
    g_bl = rgb565(bl)[1] if bl is not None else -1
    checks.append(("bottom-left green field = 63", g_bl == 63,
                   f"g={g_bl} px=0x{bl:04x}" if bl is not None else "read failed"))

    rs = []
    for i in range(5):
        p = px(f"ramp{i}")
        rs.append(rgb565(p)[0] if p is not None else -1)
    mono = len(rs) == 5 and all(rs[i] < rs[i + 1] for i in range(4))
    checks.append(("red ramps left->right", mono, f"r={rs}"))

    print()
    npass = 0
    for name, ok, detail in checks:
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:32s} {detail}")
        npass += 1 if ok else 0
    print(f"\n{ npass }/{len(checks)} checks passed")
    return 0 if npass == len(checks) else 1


if __name__ == "__main__":
    sys.exit(main())
