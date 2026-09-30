#!/usr/bin/env python3
"""Is XCLK actually ON P1011? Sample P1011 PIDR asynchronously: a 24 MHz PWM
gives random 0/1 samples; a stuck pin gives a constant level."""
import time
from pyocd.core.helpers import ConnectHelper

PFS_P1011 = 0x40400800 + 10 * 0x40 + 11 * 4

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    lv = set()
    for _ in range(300):
        try:
            w = t.read32(PFS_P1011)
            lv.add((w >> 1) & 1)
        except Exception:
            pass
    print(f"P1011 PIDR over 300 samples: {sorted(lv)}")
    print("  => " + ("PWM PRESENT on pin" if len(lv) > 1 else "PIN STUCK CONSTANT (no XCLK on the pin!)"))
    w = t.read32(PFS_P1011)
    print(f"  PFS_P1011={w:#x} PMR={(w >> 16) & 1} PSEL={(w >> 24) & 0x1F}")
