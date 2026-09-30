#!/usr/bin/env python3
"""PSEL sweep v3: PWPR unlock via 32-bit accesses + readback verification."""
import time
from pyocd.core.helpers import ConnectHelper

PFS_P1011 = 0x40400800 + 10 * 0x40 + 11 * 4
PWPR      = 0x40400D0C
ORIG      = 0x3010000

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    t.write32(PWPR, 0x00)
    t.write32(PWPR, 0x40)
    print(f"PWPR: {t.read32(PWPR) & 0xFF:#04x} (expect 0x40)")

    for psel in range(8):
        val = (psel << 24) | 0x10000
        t.write32(PFS_P1011, val)
        time.sleep(0.002)
        rb = t.read32(PFS_P1011)
        lv = set()
        for _ in range(150):
            try:
                lv.add((t.read32(PFS_P1011) >> 1) & 1)
            except Exception:
                pass
        tag = "PWM PRESENT  <-- GTIOC7B here" if len(lv) > 1 else "no PWM"
        print(f"  PSEL={psel}: rb={'OK' if rb == val else 'FAIL ' + hex(rb)} levels={sorted(lv)}  {tag}")

    t.write32(PFS_P1011, ORIG)
    print(f"restored PFS_P1011={t.read32(PFS_P1011):#x}")
