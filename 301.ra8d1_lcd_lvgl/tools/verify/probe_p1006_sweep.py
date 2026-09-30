#!/usr/bin/env python3
"""Sweep PSEL on P1006 while OUR firmware runs GPT7: is GTIOC7B here?"""
import time
from pyocd.core.helpers import ConnectHelper

PFS_P1006 = 0x40400800 + 10 * 0x40 + 6 * 4    # 0x40400A98

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    print(f"PFS_P1006 before: {t.read32(PFS_P1006):#x}")
    for psel in range(8):
        val = (psel << 24) | 0x10000
        t.write32(PFS_P1006, val)
        time.sleep(0.002)
        rb = t.read32(PFS_P1006)
        lv = set()
        for _ in range(150):
            try:
                lv.add((t.read32(PFS_P1006) >> 1) & 1)
            except Exception:
                pass
        tag = "PWM PRESENT  <-- GTIOC7B is HERE" if len(lv) > 1 else "no PWM"
        print(f"  PSEL={psel}: rb={'OK' if rb == val else 'FAIL'} levels={sorted(lv)}  {tag}")
    t.write32(PFS_P1006, 0x10000)   # leave peripheral mode, PSEL=0
    print(f"PFS_P1006 after: {t.read32(PFS_P1006):#x}")
