#!/usr/bin/env python3
"""Dump the NES window of the GLCDC framebuffer as a coarse ASCII map.

Reads the centred 240x240 region the emulator blits into and prints one
character per 8x8 block, chosen by the dominant RGB565 colour.  This is a
diagnostic: it answers "what is actually on the panel" without a camera, and
distinguishes a rendered frame (varied, structured) from a frozen or blank one.

Usage:
    python tools/verify/dump_fb.py
"""
import os
import subprocess
import sys
import tempfile
import time

FB_BASE = 0x68000000
FB_STRIDE = 480
DST_X = 120
DST_Y = 60
SIZE = 240
BLOCK = 8
STEP = SIZE // BLOCK


def pyocd_read(addr, nbytes, timeout=180):
    fd, path = tempfile.mkstemp(suffix=".pyocd_cmds", text=True)
    try:
        with os.fdopen(fd, "w") as f:
            f.write("\n".join(
                [f"read16 {hex(addr)} {nbytes}" for _ in range(1)]
            ) + "\ngo\n")
        r = subprocess.run(
            ["pyocd", "cmd", "-t", "cortex_m", "-O", "connect_mode=pre-reset",
             "-x", path],
            capture_output=True, text=True, timeout=timeout,
        )
        return r.stdout
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def read_block(addr, count):
    """Read `count` 16-bit halves starting at addr in one pyocd session."""
    out = pyocd_read(addr, count * 2)
    vals = []
    for line in out.splitlines():
        if ":" not in line:
            continue
        rhs = line.split(":", 1)[1]
        rhs = rhs.split("|", 1)[0]
        for tok in rhs.split():
            if len(tok) == 4:
                try:
                    vals.append(int(tok, 16))
                except ValueError:
                    pass
    return vals


def rgb565_to_name(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    if r < 4 and g < 8 and b < 4:
        return "."
    if r > 24 and g > 48 and b > 24:
        return "W"
    if r > 20 and g < 12 and b < 12:
        return "R"
    if g > 40 and r < 12 and b < 12:
        return "G"
    if b > 20 and r < 12 and g < 12:
        return "B"
    if r > 20 and g > 40 and b < 12:
        return "Y"
    if r > 20 and b > 20 and g < 12:
        return "M"
    if g > 30 and b > 30 and r < 12:
        return "C"
    return "?"


def main():
    print(f"reading {STEP}x{STEP} blocks from framebuffer window "
          f"({DST_X},{DST_Y})+{SIZE}x{SIZE}, stride {FB_STRIDE}")
    rows = []
    for by in range(STEP):
        y = DST_Y + by * BLOCK
        addr = FB_BASE + 2 * (FB_STRIDE * y + DST_X)
        vals = read_block(addr, SIZE)
        if len(vals) < SIZE:
            print(f"row {by}: short read ({len(vals)})", file=sys.stderr)
            vals += [0] * (SIZE - len(vals))
        line = ""
        for i in range(STEP):
            seg = vals[i * BLOCK:(i + 1) * BLOCK]
            common = max(set(seg), key=seg.count) if seg else 0
            line += rgb565_to_name(common)
        rows.append(line)
        print(f"{by:2d} |{line}|")
    uniq = set("".join(rows))
    print(f"symbols seen: {''.join(sorted(uniq))}")


if __name__ == "__main__":
    main()
