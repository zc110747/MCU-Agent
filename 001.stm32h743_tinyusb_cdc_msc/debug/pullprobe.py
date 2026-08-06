#!/usr/bin/env python3
"""Tell a driven DVP line from a disconnected one, without a scope.

    python debug/pullprobe.py

Runs bsp_camera_probe_pull(), which samples the eleven DVP signals three times
over - floating, pulled up, pulled down - and reports the AND/OR of every
sample. A line the sensor actually drives ignores the ~40 kOhm internal pull
and reads the same in all three passes. A line that is not connected simply
follows the resistor.

The sync lines (HSYNC / VSYNC / PIXCLK) are the positive control: they are
known good, so they must stay identical across the three passes.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPENOCD = os.environ.get("OPENOCD", r"D:/Software/openocd/bin/openocd.exe")
SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", r"D:/Software/openocd/share/openocd/scripts")
CFG = os.path.join(ROOT, "debug", "openocd.cfg").replace("\\", "/")
ELF = os.path.join(ROOT, "build", "stm32h743_uvc.elf")

NAMES = ["D0 PC6", "D1 PC7", "D2 PG10", "D3 PG11", "D4 PE4", "D5 PD3",
         "D6 PE5", "D7 PE6", "HSYNC PA4", "VSYNC PG9", "PCLK PA6"]
PASSES = ["float", "pull-up", "pull-dn"]


def symbols(*names):
    txt = subprocess.run(["arm-none-eabi-nm", ELF], capture_output=True, text=True).stdout
    table = {}
    for line in txt.splitlines():
        p = line.split()
        if len(p) == 3:
            table[p[2]] = int(p[0], 16)
    missing = [n for n in names if n not in table]
    if missing:
        raise SystemExit(f"symbol(s) not in ELF: {missing}")
    return [table[n] for n in names]


def main():
    a_and, a_or, req, done = symbols(
        "cam_pull_and", "cam_pull_or", "cam_pull_req", "cam_pull_done")

    argv = [OPENOCD, "-s", SCRIPTS, "-f", CFG, "-c", "init",
            "-c", "reset run", "-c", "sleep 1500",
            "-c", f"mww 0x{req:08x} 1", "-c", "sleep 3000",
            "-c", f"mdw 0x{done:08x} 1",
            "-c", f"mdw 0x{a_and:08x} 3",
            "-c", f"mdw 0x{a_or:08x} 3",
            "-c", "shutdown"]
    out = subprocess.run(argv, capture_output=True, text=True)
    text = out.stdout + out.stderr

    def words(addr, n):
        m = re.search(rf"0x{addr:08x}:((?:\s+[0-9a-f]{{8}}){{{n}}})", text)
        if not m:
            sys.stderr.write(text)
            raise SystemExit(f"could not read 0x{addr:08x}")
        return [int(x, 16) for x in m.group(1).split()]

    if words(done, 1)[0] == 0:
        sys.stderr.write(text)
        raise SystemExit("probe never ran (cam_pull_done still 0)")

    ands = words(a_and, 3)
    ors = words(a_or, 3)

    print(f"{'signal':<11} " + "  ".join(f"{p:^9}" for p in PASSES) + "   verdict")
    print("-" * 62)

    floating = []
    for bit, name in enumerate(NAMES):
        cells, state = [], []
        for k in range(3):
            lo = (ands[k] >> bit) & 1
            hi = (ors[k] >> bit) & 1
            if lo == hi:
                cells.append(f"{'  high  ' if lo else '  low   '}")
                state.append(lo)
            else:
                cells.append("  TOGGLE")
                state.append(2)

        if state[1] == 1 and state[2] == 0:
            verdict = "FLOATING - not connected"
            floating.append(name)
        elif 2 in state:
            verdict = "driven (toggling)"
        elif state[0] == state[1] == state[2]:
            verdict = f"driven {'high' if state[0] else 'low'} (pull loses)"
        else:
            verdict = "inconsistent"
        print(f"{name:<11} " + "  ".join(f"{c:^9}" for c in cells) + f"   {verdict}")

    print()
    if floating:
        print(f"FLOATING lines ({len(floating)}): {', '.join(floating)}")
        print("These pins are not electrically connected to the OV5640.")
    else:
        print("No floating lines: every DVP pin is driven by the sensor.")


if __name__ == "__main__":
    main()
