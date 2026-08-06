#!/usr/bin/env python3
"""Trigger the firmware's DVP pin probe and print a per-signal verdict.

    python debug/pinprobe.py

The 11 DCMI signals are briefly detached from the peripheral and sampled as
raw GPIO inputs, which separates "the DCMI is misconfigured" from "this wire
carries nothing".
"""

import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPENOCD = os.environ.get("OPENOCD", r"D:/Software/openocd/bin/openocd.exe")
SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", r"D:/Software/openocd/share/openocd/scripts")
CFG = os.path.join(ROOT, "debug", "openocd.cfg").replace("\\", "/")
ELF = os.path.join(ROOT, "build", "stm32h743_uvc.elf")

NAMES = ["D0  PC6", "D1  PC7", "D2  PG10", "D3  PG11", "D4  PE4",
         "D5  PD3", "D6  PE5", "D7  PE6", "HSYNC PA4", "VSYNC PG9", "PCLK  PA6"]


def syms():
    out = subprocess.run(["arm-none-eabi-nm", ELF], capture_output=True,
                         text=True, check=True).stdout
    want = {"cam_probe_req", "cam_pin_ones", "cam_pin_zeros",
            "cam_pin_edges", "cam_probe_done"}
    return {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines())
            if len(p) == 3 and p[2] in want}


def ocd(*cmds):
    argv = [OPENOCD, "-s", SCRIPTS, "-f", CFG, "-c", "init"]
    for c in cmds:
        argv += ["-c", c]
    argv += ["-c", "shutdown"]
    p = subprocess.run(argv, capture_output=True, text=True)
    return p.stdout + p.stderr


def read_words(blob):
    vals = {}
    for m in re.finditer(r"0x([0-9a-fA-F]{8}):((?:\s+[0-9a-fA-F]{8})+)", blob):
        base = int(m.group(1), 16)
        for i, w in enumerate(m.group(2).split()):
            vals[base + 4 * i] = int(w, 16)
    return vals


def main():
    s = syms()
    before = read_words(ocd(f"mdw 0x{s['cam_probe_done']:08x} 1"))
    done0 = before.get(s["cam_probe_done"], 0)

    ocd(f"mww 0x{s['cam_probe_req']:08x} 1")
    time.sleep(1.0)

    blob = ocd(f"mdw 0x{s['cam_probe_done']:08x} 1",
               f"mdw 0x{s['cam_pin_ones']:08x} 1",
               f"mdw 0x{s['cam_pin_zeros']:08x} 1",
               f"mdw 0x{s['cam_pin_edges']:08x} 11")
    v = read_words(blob)

    if v.get(s["cam_probe_done"], 0) == done0:
        raise SystemExit("probe never ran - is bsp_camera_service() being called?")

    ones = v[s["cam_pin_ones"]]
    zeros = v[s["cam_pin_zeros"]]
    edges = [v.get(s["cam_pin_edges"] + 4 * i, 0) for i in range(11)]

    print(f"cam_pin_ones = 0x{ones:03x}   cam_pin_zeros = 0x{zeros:03x}\n")
    print(f"  {'signal':<11}{'saw 1':>7}{'saw 0':>7}{'edges':>10}   verdict")
    print("  " + "-" * 52)
    bad = []
    for i, n in enumerate(NAMES):
        hi = bool(ones & (1 << i))
        lo = bool(zeros & (1 << i))
        if hi and lo:
            verdict = "toggling"
        elif hi:
            verdict = "STUCK HIGH"
            bad.append(n)
        elif lo:
            verdict = "STUCK LOW"
            bad.append(n)
        else:
            verdict = "?"
            bad.append(n)
        print(f"  {n:<11}{str(hi):>7}{str(lo):>7}{edges[i]:>10}   {verdict}")

    print()
    if bad:
        print("DEAD SIGNALS: " + ", ".join(bad))
        print("A stuck DVP line is a hardware/pinmux fault - the DCMI cannot fix it.")
    else:
        print("all 11 DVP signals are alive")


if __name__ == "__main__":
    main()
