#!/usr/bin/env python3
"""One-shot SWD probe: dump GLCDC GR[0]/GR[1] registers to settle the L2UNDF question."""
import sys

from pyocd.core.helpers import ConnectHelper

# GLCDC base 0x40342000, GR[] at +0x1100, stride 0x100, SYSCNT at +0x1440.
GR0 = 0x40343100
GR1 = 0x40343200
SYSCNT_STCLR = 0x40343448
SYSCNT_STMON = 0x4034344C

REGS = [
    ("GR1.VEN    (PVEN)",        GR1 + 0x00),
    ("GR1.FLMRD  (RENB)",        GR1 + 0x04),
    ("GR1.FLM2   (BASE)",        GR1 + 0x0C),
    ("GR1.AB1    (DISPSEL)",     GR1 + 0x20),
    ("GR1.CLUTINT(LINE,SEL)",    GR1 + 0x50),
    ("GR1.MON    (UNDFLST)",     GR1 + 0x54),
    ("GR0.VEN    (PVEN)",        GR0 + 0x00),
    ("GR0.FLMRD  (RENB)",        GR0 + 0x04),
    ("GR0.FLM2   (BASE)",        GR0 + 0x0C),
    ("GR0.MON    (UNDFLST)",     GR0 + 0x54),
    ("SYSCNT.STCLR",             SYSCNT_STCLR),
    ("SYSCNT.STMON",             SYSCNT_STMON),
]

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=1000000,
                                             halt_on_connect=False) as session:
    tgt = session.target
    for name, addr in REGS:
        val = tgt.read32(addr)
        print("%-22s @0x%08X = 0x%08X" % (name, addr, val))
    # clear L2UNDF + VPOS once, wait 0.5 s (several frames), re-read to see if it re-sets
    print("--- writing STCLR (VPOSCLR|L1UNDFCLR|L2UNDFCLR = 0x7) ---")
    tgt.write32(SYSCNT_STCLR, 0x7)
    import time
    time.sleep(0.5)
    for name, addr in REGS:
        val = tgt.read32(addr)
        print("%-22s @0x%08X = 0x%08X" % (name, addr, val))
