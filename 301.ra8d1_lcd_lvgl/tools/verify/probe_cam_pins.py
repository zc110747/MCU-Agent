#!/usr/bin/env python3
"""Phase 3 debug probe: PFS pin states + GPT7 activity + SCCB line levels.

Run while the firmware is up (after `cam init` attempt). Read-only.
"""
import sys
from pyocd.core.helpers import ConnectHelper
from pyocd.core.memory_map import MemoryType

PFS_BASE = 0x40400800
STRIDE = 0x40  # per port

def pfs(port, pin):
    return PFS_BASE + port * STRIDE + pin * 4

PINS = {
    "P1103 SCCB_SCL": (11, 3),
    "P50E  SCCB_SDA": (5, 14),
    "P1011 XCLK/BKLT": (10, 11),
    "P704  CAM_RESET": (7, 4),
    "P705  CAM_PWDN": (7, 5),
    "P709  CEU_VSYNC": (7, 9),
    "P708  CEU_PCLK": (7, 8),
}

GPT7 = 0x40322700
# offsets per R7FA8D1BH.h R_GPT0_Type
GTCR, GTSTR, GTUDDTYC, GTIOR, GTCNT, GTPR = 0x2C, 0x04, 0x30, 0x34, 0x48, 0x64

# PFS word: PODR[0] PIDR[1] PDR[2] PCR[4] DSCR[11:10] PMR[16] PSEL[28:24]
def describe(v):
    return (f"PODR={v & 1} PIDR={(v >> 1) & 1} PDR={(v >> 2) & 1} "
            f"PCR={(v >> 4) & 1} DSCR={(v >> 10) & 3} PMR={(v >> 16) & 1} "
            f"PSEL={(v >> 24) & 0x1F} raw=0x{v:08X}")

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=1000000,
                                             halt_on_connect=False) as session:
    t = session.target
    print("== PFS pin states ==")
    for name, (p, n) in PINS.items():
        v = t.read32(pfs(p, n))
        print(f"  {name:18s} {describe(v)}")

    print("== GPT7 (XCLK 24MHz expected: GTPR=4, GTCCR0=2, GTCR.TCST=1) ==")
    for name, off in (("GTCR", GTCR), ("GTSTR", GTSTR), ("GTUDDTYC", GTUDDTYC),
                      ("GTIOR", GTIOR), ("GTCNT", GTCNT), ("GTPR", GTPR)):
        try:
            print(f"  {name:8s} 0x{t.read32(GPT7 + off):08X}")
        except Exception as e:
            print(f"  {name:8s} ERR {e}")
    try:
        c1 = t.read32(GPT7 + GTCNT)
        c2 = t.read32(GPT7 + GTCNT)
        print(f"  GTCNT delta: {(c2 - c1) & 0xFFFFFFFF}  (c1={c1}, c2={c2})")
    except Exception as e:
        print(f"  GTCNT ERR {e}")
