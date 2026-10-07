#!/usr/bin/env python3
"""Independent check that the NES port layer actually presents frames to SDRAM.

Why this exists
---------------
`nes info` reports a draw counter.  A counter only proves the port's bookkeeping
ran -- it does not prove the DRW2D blit wrote the emulator's pixels into the
GLCDC framebuffer at the right place.  This script reads the framebuffer back
over SWD and compares exact RGB565 values, which is the same evidence tier the
Phase 1 GPU check used (reproducible measurement, not inference).

What it verifies
----------------
1. `nes test` renders a synthetic 240x240 frame made of four flat colour bands
   (red / green / blue / white) through the *real* nes_draw() path.  Because the
   bands are saturated, exactly-representable RGB565 constants, every sample can
   be compared with `==` rather than a tolerance.
2. The same sample points outside the 240x240 window must still be black, which
   proves the destination offset is (120, 60) and not (0, 0).

Framebuffer: 0x68000000, 480x360 RGB565, stride 480 px.
NES window:  240x240 at dst (120, 60).

Usage:
    python tools/verify/verify_nes_frame.py --render   # trigger then verify
    python tools/verify/verify_nes_frame.py            # verify only
"""
import os
import re
import subprocess
import sys
import tempfile
import time

import serial

PORT = "COM9"
BAUD = 115200

FB_BASE = 0x68000000
FB_STRIDE = 480

NES_W = 240
NES_H = 240
DST_X = (480 - NES_W) // 2     # 120
DST_Y = (360 - NES_H) // 2     # 60

# Flat bands produced by `nes test`, as raw RGB565 words.
BAND_RED = 0xF800
BAND_GREEN = 0x07E0
BAND_BLUE = 0x001F
BAND_WHITE = 0xFFFF


def send_serial(cmd, settle=1.5, expect=None):
    """Write a command and return what the board said.

    Routed through the shared helper (see verify_nes_rom.send_serial) rather than
    a bare write: the shell goes deaf after the firmware has been running for a
    while, and the only reliable cure is a reset first.  `expect`, when given, is
    a regex that must appear in the response - the caller then knows the command
    actually ran instead of assuming it did.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from verify_nes_rom import send_serial as _send

    text = _send([cmd], settle=settle, listen_s=2.5)
    if expect is not None and not re.search(expect, text):
        raise RuntimeError(f"command {cmd!r} did not take effect; got: {text[-400:]!r}")
    return text


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


def main():
    if "--render" in sys.argv:
        # Reset first: the shell stops answering after the firmware has been up
        # for a while, and a silent `nes test` would leave the window black and
        # look like a render failure.  `expect` then proves it really ran.
        subprocess.run(
            ["pyocd", "reset", "-t", "cortex_m", "-O", "connect_mode=pre-reset"],
            capture_output=True, timeout=90,
        )
        time.sleep(2.5)
        send_serial("nes test", settle=1.0, expect=r"nes test")
        time.sleep(1.5)

    # (label, absolute fb x, absolute fb y, expected RGB565)
    # Local NES y is banded every 60 rows:     0-59 red, 60-119 green,
    # 120-179 blue, 180-239 white.
    points = [
        # --- inside the NES window: sampled one row/col in from each corner
        ("in_red_tl",     DST_X,          DST_Y,          BAND_RED),
        ("in_red_br",     DST_X + 239,    DST_Y + 59,     BAND_RED),
        ("in_green_tl",   DST_X,          DST_Y + 60,     BAND_GREEN),
        ("in_green_br",   DST_X + 239,    DST_Y + 119,    BAND_GREEN),
        ("in_blue_tl",    DST_X,          DST_Y + 120,    BAND_BLUE),
        ("in_blue_br",    DST_X + 239,    DST_Y + 179,    BAND_BLUE),
        ("in_white_tl",   DST_X,          DST_Y + 180,    BAND_WHITE),
        ("in_white_br",   DST_X + 239,    DST_Y + 239,    BAND_WHITE),
        ("in_cent",       DST_X + 120,    DST_Y + 120,    BAND_BLUE),
        # --- just outside the window: must still be black
        ("out_left",      DST_X - 1,      DST_Y,          0x0000),
        ("out_right",     DST_X + 240,    DST_Y,          0x0000),
        ("out_top",       DST_X,          DST_Y - 1,      0x0000),
        ("out_bottom",    DST_X,          DST_Y + 240,    0x0000),
        ("out_tl_panel",  0,              0,              0x0000),
        ("out_br_panel",  479,            359,            0x0000),
    ]

    # Extra probes for the band-structure check.  read_many() only fetches the
    # addresses it is given, so these must be in the same read set.
    band_probe_dy = (10, 70, 130, 190)
    band_probe_addrs = [px_addr(DST_X + 10, DST_Y + dy) for dy in band_probe_dy]

    table = read_many([px_addr(x, y) for (_, x, y, _) in points] + band_probe_addrs)

    if not table:
        print("\n  [FAIL] SWD read-back returned nothing (probe enumeration?)")
        print("\n0/1 checks passed")
        return 1

    checks = []
    for (label, x, y, expect) in points:
        v = table.get(px_addr(x, y))
        got = None if v is None else (v & 0xFFFF)
        kind = "outside window" if label.startswith("out_") else "in window"
        checks.append((f"{label} ({kind}) = 0x{expect:04x}",
                       got == expect,
                       f"0x{got:04x}" if got is not None else "read failed"))

    # The four bands must be mutually distinct, which rules out a blit that
    # wrote one colour everywhere.
    band_vals = [table.get(a) for a in band_probe_addrs]
    band_vals = [None if v is None else (v & 0xFFFF) for v in band_vals]
    checks.append(("four bands distinct",
                   len(set(band_vals)) == 4 and None not in band_vals,
                   f"{['0x%04x' % v if v is not None else '?' for v in band_vals]}"))

    print()
    npass = 0
    for name, ok, detail in checks:
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:44s} {detail}")
        npass += 1 if ok else 0
    print(f"\n{npass}/{len(checks)} checks passed")
    return 0 if npass == len(checks) else 1


if __name__ == "__main__":
    sys.exit(main())
