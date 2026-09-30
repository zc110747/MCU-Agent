#!/usr/bin/env python3
"""PSEL sweep v2 on P1011 with PWPR unlock + readback verification.
Finds which PSEL carries the GTIOC7B 24 MHz output (if any)."""
import time

from pyocd.core.helpers import ConnectHelper

PFS_P1011 = 0x40400800 + 10 * 0x40 + 11 * 4
PWPR      = 0x40400D0C          # R_PMISC + 0x0C (PFSWE bit6, B0WI bit7)
ORIG      = 0x3010000

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target

    # unlock PFS writes (B0WI=0 then PFSWE=1)
    t.write8(PWPR, 0x00)
    t.write8(PWPR, 0x40)
    print(f"PWPR after unlock: {t.read8(PWPR):#04x} (expect 0x40)")

    print("sweeping PSEL 0..7 with readback, 150 async PIDR samples each...")
    for psel in range(8):
        val = (psel << 24) | 0x10000
        t.write32(PFS_P1011, val)
        time.sleep(0.002)
        rb = t.read32(PFS_P1011)
        ok = (rb == val)
        lv = set()
        for _ in range(150):
            try:
                lv.add((t.read32(PFS_P1011) >> 1) & 1)
            except Exception:
                pass
        tag = "PWM PRESENT  <-- GTIOC7B here" if len(lv) > 1 else "no PWM"
        print(f"  PSEL={psel}: rb={'OK ' if ok else 'FAIL ' + hex(rb)} levels={sorted(lv)}  {tag}")

    t.write32(PFS_P1011, ORIG)
    print(f"restored PFS_P1011={t.read32(PFS_P1011):#x}")
