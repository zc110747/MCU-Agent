#!/usr/bin/env python3
"""Pull the live DCMI frame buffer out of AXI SRAM over SWD and analyse it.

    python debug/fbdump.py

Bypasses USB entirely, so it separates "the sensor/DCMI produced a bad frame"
from "the UVC transport mangled a good one".
"""

import os
import subprocess
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPENOCD = os.environ.get("OPENOCD", r"D:/Software/openocd/bin/openocd.exe")
SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", r"D:/Software/openocd/share/openocd/scripts")
CFG = os.path.join(ROOT, "debug", "openocd.cfg").replace("\\", "/")

W, H = 240, 240
NBYTES = W * H * 2
FB_ADDR = 0x24000000
OUT = os.path.join(ROOT, "debug", "out")


def dump(addr, nbytes, path):
    path = path.replace("\\", "/")
    cmds = [OPENOCD, "-s", SCRIPTS, "-f", CFG, "-c", "init",
            "-c", f"dump_image {path} 0x{addr:08x} {nbytes}", "-c", "shutdown"]
    p = subprocess.run(cmds, capture_output=True, text=True)
    if not os.path.exists(path) or os.path.getsize(path) != nbytes:
        sys.stderr.write(p.stdout + p.stderr)
        raise SystemExit("dump_image failed")


def analyse(raw, label):
    b = np.frombuffer(raw, dtype=np.uint8)
    yuy2 = b.reshape(H, W // 2, 4)
    y = np.empty((H, W), dtype=np.uint8)
    y[:, 0::2] = yuy2[:, :, 0]
    y[:, 1::2] = yuy2[:, :, 2]
    u = yuy2[:, :, 1]
    v = yuy2[:, :, 3]

    print(f"--- {label} ---")
    print(f"  Y  min={y.min():3d} max={y.max():3d} mean={y.mean():6.1f} std={y.std():6.2f}")
    print(f"  U  min={u.min():3d} max={u.max():3d} mean={u.mean():6.1f} std={u.std():6.2f}")
    print(f"  V  min={v.min():3d} max={v.max():3d} mean={v.mean():6.1f} std={v.std():6.2f}")
    print(f"  unique bytes in whole frame : {len(np.unique(b))}")
    print(f"  distinct values per row (avg): {np.mean([len(np.unique(r)) for r in b.reshape(H, -1)]):.1f}")

    # Horizontal neighbour delta tells a real image from a latched constant.
    dx = np.abs(np.diff(y.astype(int), axis=1)).mean()
    dy = np.abs(np.diff(y.astype(int), axis=0)).mean()
    print(f"  mean |dY/dx| = {dx:.2f}   mean |dY/dy| = {dy:.2f}")

    print(f"  first 16 bytes of row 0   : {b[:16].tolist()}")
    print(f"  first 16 bytes of row 120 : {b[120 * W * 2:120 * W * 2 + 16].tolist()}")
    return y, u, v


def main():
    os.makedirs(OUT, exist_ok=True)
    p1 = os.path.join(OUT, "fb_a.bin")
    p2 = os.path.join(OUT, "fb_b.bin")

    dump(FB_ADDR, NBYTES, p1)
    dump(FB_ADDR, NBYTES, p2)

    a = open(p1, "rb").read()
    b = open(p2, "rb").read()

    ya, _, _ = analyse(a, "snapshot A")
    yb, _, _ = analyse(b, "snapshot B")

    diff = (np.frombuffer(a, np.uint8) != np.frombuffer(b, np.uint8)).sum()
    print(f"\nA vs B: {diff}/{NBYTES} bytes differ ({100.0 * diff / NBYTES:.1f}%)"
          "   <- should be well above 0 on a live sensor")

    try:
        import cv2
        yuy2 = np.frombuffer(a, np.uint8).reshape(H, W, 2)
        bgr = cv2.cvtColor(yuy2, cv2.COLOR_YUV2BGR_YUY2)
        cv2.imwrite(os.path.join(OUT, "fb_swd.png"), bgr)
        cv2.imwrite(os.path.join(OUT, "fb_swd_luma.png"), ya)
        print(f"saved {OUT}/fb_swd.png and fb_swd_luma.png")
    except Exception as e:
        print(f"(no PNG: {e})")


if __name__ == "__main__":
    main()
