#!/usr/bin/env python3
"""Decode the raw DVP bus trace captured by bsp_camera_trace_bus().

    python debug/bustrace.py

The firmware locks onto an HSYNC rising edge and then reads GPIOA and GPIOE
back-to-back at full load bandwidth, parking the untouched IDR words in DTCM.
This script pulls cam_trace[] out over SWD and answers the single question the
free-running probes could not:

    while PIXCLK is toggling, does the data bus move at all?

Slot layout (interleaved, 4096 words total):
    even  GPIOA->IDR  bit4 = HSYNC (PA4)   bit6 = PIXCLK (PA6)
    odd   GPIOE->IDR  bit4 = D4 (PE4)  bit5 = D6 (PE5)  bit6 = D7 (PE6)
"""

import os
import re
import subprocess
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPENOCD = os.environ.get("OPENOCD", r"D:/Software/openocd/bin/openocd.exe")
SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", r"D:/Software/openocd/share/openocd/scripts")
CFG = os.path.join(ROOT, "debug", "openocd.cfg").replace("\\", "/")
ELF = os.path.join(ROOT, "build", "stm32h743_uvc.elf")
OUT = os.path.join(ROOT, "debug", "out")

NSLOTS = 4096
CPU_HZ = 480e6


def symbols(*names):
    """Resolve globals to addresses straight out of the ELF."""
    txt = subprocess.run(["arm-none-eabi-nm", ELF], capture_output=True, text=True).stdout
    table = {}
    for line in txt.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    missing = [n for n in names if n not in table]
    if missing:
        raise SystemExit(f"symbol(s) not in ELF: {missing}")
    return [table[n] for n in names]


def openocd(*cmds):
    argv = [OPENOCD, "-s", SCRIPTS, "-f", CFG, "-c", "init"]
    for c in cmds:
        argv += ["-c", c]
    argv += ["-c", "shutdown"]
    p = subprocess.run(argv, capture_output=True, text=True)
    return p.stdout + p.stderr


def capture():
    """Grab one trace. Pass --pattern to arm the sensor's colour-bar generator
    first: bars are injected after the ISP but before the DVP output stage, so
    a still bus under bars blames the output stage and a moving one blames the
    imaging path."""
    os.makedirs(OUT, exist_ok=True)
    trace, req, done = symbols("cam_trace", "cam_trace_req", "cam_trace_done")
    binpath = os.path.join(OUT, "bustrace.bin").replace("\\", "/")

    pre = []
    if "--pattern" in sys.argv:
        (pat,) = symbols("cam_test_pattern")
        pre = [f"mww 0x{pat:08x} 1", "sleep 700"]
        print("*** sensor colour-bar generator ENABLED (0x503D = 0x80) ***\n")

    out = openocd(
        "reset run", "sleep 1500",
        *pre,
        f"mww 0x{req:08x} 1", "sleep 400",
        f"mdw 0x{done:08x} 1",
        f"dump_image {binpath} 0x{trace:08x} {NSLOTS * 4}",
    )
    m = re.search(rf"0x{done:08x}:\s*([0-9a-f]+)", out)
    if not m or int(m.group(1), 16) == 0:
        sys.stderr.write(out)
        raise SystemExit("trace never completed (cam_trace_done still 0)")
    if not os.path.exists(binpath):
        sys.stderr.write(out)
        raise SystemExit("dump_image failed")
    return np.fromfile(binpath, dtype="<u4")


def main():
    w = capture()
    a = w[0::2]  # GPIOA
    e = w[1::2]  # GPIOE

    hsync = (a >> 4) & 1
    pclk = (a >> 6) & 1
    d4 = (e >> 4) & 1
    d6 = (e >> 5) & 1
    d7 = (e >> 6) & 1
    data3 = (d7 << 2) | (d6 << 1) | d4

    n = len(a)
    pclk_edges = int(np.count_nonzero(np.diff(pclk)))
    hsync_edges = int(np.count_nonzero(np.diff(hsync)))
    data_edges = int(np.count_nonzero(np.diff(data3)))

    print(f"pairs captured           : {n}")
    print(f"PIXCLK edges             : {pclk_edges}"
          f"   (high {pclk.mean() * 100:.1f}% of the window)")
    print(f"HSYNC  edges             : {hsync_edges}"
          f"   (high {hsync.mean() * 100:.1f}% of the window)")
    print(f"D4/D6/D7 combined edges  : {data_edges}")
    print(f"distinct D4/D6/D7 codes  : {sorted(np.unique(data3).tolist())}")
    for name, sig in (("D4", d4), ("D6", d6), ("D7", d7)):
        print(f"  {name}: high {sig.mean() * 100:5.1f}%  "
              f"edges {int(np.count_nonzero(np.diff(sig))):5d}")

    if pclk_edges:
        spp = 2.0 * n / pclk_edges  # samples per PIXCLK period
        print(f"\nsamples per PIXCLK period: {spp:.1f}"
              f"   -> PIXCLK ~= {CPU_HZ / (spp * 8.0) / 1e6:.1f} MHz (rough)")

    print("\nfirst 48 pairs  (P=PIXCLK H=HSYNC, then D7 D6 D4):")
    for i in range(min(48, n)):
        print(f"  {i:3d}  P{pclk[i]} H{hsync[i]}   "
              f"{d7[i]}{d6[i]}{d4[i]}")

    print("\n--- verdict ---")
    if pclk_edges < 10:
        print("  PIXCLK is not toggling: the sensor clock output is dead.")
    elif data_edges == 0:
        print("  PIXCLK toggles but D4/D6/D7 NEVER move across the whole window.")
        print("  The data pads are not driving pixel content - blame the sensor")
        print("  data-pad config or the DVP wiring, not the DCMI.")
    else:
        rate = data_edges / max(pclk_edges / 2.0, 1.0)
        print(f"  Data moves: {data_edges} edges over {pclk_edges // 2} PIXCLK")
        print(f"  periods ({rate:.2f} data edges per clock).")
        print("  The bus carries real pixels - the DCMI is latching them wrong.")


if __name__ == "__main__":
    main()
