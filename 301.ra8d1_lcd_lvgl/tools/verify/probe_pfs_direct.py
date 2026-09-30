#!/usr/bin/env python3
"""Direct PFS experiment on P1103/P50E/P704 via SWD:
1. snapshot current PFS
2. force P1103 PDR=1 PODR=0 (drive low) -> does PIDR follow?
3. restore
"""
from pyocd.core.helpers import ConnectHelper

PFS_BASE = 0x40400800
SCL = PFS_BASE + 11 * 0x40 + 3 * 4    # P1103
SDA = PFS_BASE + 5 * 0x40 + 14 * 4    # P50E
RST = PFS_BASE + 7 * 0x40 + 4 * 4     # P704

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=1000000,
                                             halt_on_connect=False) as session:
    t = session.target
    for name, addr in (("P1103", SCL), ("P50E", SDA), ("P704", RST)):
        v0 = t.read32(addr)
        # drive low: PDR=1(bit2), PODR=0(bit0), keep PCR/PMR etc from v0 but clear PIDR noise bits
        t.write32(addr, (v0 & ~0x1) | 0x4)   # PODR=0, PDR=1
        time.sleep(0)  # no sleep import needed; SWD write is immediate
        v1 = t.read32(addr)
        pidr_after_low = (v1 >> 1) & 1
        # drive high
        t.write32(addr, (v0 & ~0x1) | 0x5)   # PODR=1, PDR=1
        v2 = t.read32(addr)
        pidr_after_high = (v2 >> 1) & 1
        # restore
        t.write32(addr, v0)
        print(f"{name}: orig=0x{v0:08X} | drive-low PIDR={pidr_after_low} (0x{v1:08X})"
              f" | drive-high PIDR={pidr_after_high} (0x{v2:08X})"
              f" => {'PIN FOLLOWS' if pidr_after_low == 0 and pidr_after_high == 1 else 'PIN STUCK'}")
