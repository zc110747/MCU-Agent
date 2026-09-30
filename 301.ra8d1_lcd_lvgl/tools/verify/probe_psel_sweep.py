#!/usr/bin/env python3
"""PSEL sweep on P1011 while GPT7 runs: find which PSEL actually carries
the 24 MHz GTIOC7B output. Restores the original PFS value afterwards."""
import time
from pyocd.core.helpers import ConnectHelper

PFS_P1011 = 0x40400800 + 10 * 0x40 + 11 * 4
ORIG      = 0x3010000

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    print("sweeping PSEL 0..7 (PMR=1), 150 async PIDR samples each...")
    for psel in range(8):
        t.write32(PFS_P1011, (psel << 24) | 0x10000)
        time.sleep(0.002)
        lv = set()
        for _ in range(150):
            try:
                lv.add((t.read32(PFS_P1011) >> 1) & 1)
            except Exception:
                pass
        tag = "PWM PRESENT  <-- GTIOC7B is here" if len(lv) > 1 else "no PWM"
        print(f"  PSEL={psel}: levels={sorted(lv)}  {tag}")
    t.write32(PFS_P1011, ORIG)
    w = t.read32(PFS_P1011)
    print(f"restored PFS_P1011={w:#x}")
