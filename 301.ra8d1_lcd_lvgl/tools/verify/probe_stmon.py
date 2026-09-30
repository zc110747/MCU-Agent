#!/usr/bin/env python3
"""Behavior curve probe: does STMON.L2UNDF oscillate or latch? When does it re-assert after clear?"""
import time

from pyocd.core.helpers import ConnectHelper

STCLR = 0x40343448
STMON = 0x4034344C
GR1_MON = 0x40343254

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=1000000,
                                             halt_on_connect=False) as session:
    tgt = session.target
    print("== phase A: free-run sampling (no clear), 10 x 200ms ==")
    for i in range(10):
        stmon = tgt.read32(STMON)
        gr1mon = tgt.read32(GR1_MON)
        print("t=%4dms  STMON=0x%08X (VPOS=%d L1=%d L2=%d)  GR1.MON.UNDFLST=%d"
              % (i * 200, stmon, stmon & 1, (stmon >> 1) & 1, (stmon >> 2) & 1,
                 (gr1mon >> 16) & 1))
        time.sleep(0.2)
    print("== phase B: clear once, then poll every 100ms for 1s ==")
    tgt.write32(STCLR, 0x7)
    for i in range(10):
        time.sleep(0.1)
        stmon = tgt.read32(STMON)
        gr1mon = tgt.read32(GR1_MON)
        print("t=%4dms  STMON=0x%08X (VPOS=%d L1=%d L2=%d)  GR1.MON.UNDFLST=%d"
              % (100 + i * 100, stmon, stmon & 1, (stmon >> 1) & 1, (stmon >> 2) & 1,
                 (gr1mon >> 16) & 1))
    print("== phase C: write L2UNDFCLR only, re-read GR1.MON ==")
    tgt.write32(STCLR, 0x4)
    time.sleep(0.1)
    stmon = tgt.read32(STMON)
    gr1mon = tgt.read32(GR1_MON)
    print("STMON=0x%08X  GR1.MON=0x%08X" % (stmon, gr1mon))
