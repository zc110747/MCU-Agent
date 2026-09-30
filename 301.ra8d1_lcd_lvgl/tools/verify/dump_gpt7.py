#!/usr/bin/env python3
"""Dump GPT7 register window + P1011 PFS. Run while the target firmware has
the camera XCLK running (official demo or our cam init)."""
import sys
import time

from pyocd.core.helpers import ConnectHelper

GPT7      = 0x40322700
PFS_P1011 = 0x40400800 + 10 * 0x40 + 11 * 4
REGS = [("GTSTR", 0x04), ("GTCR", 0x2C), ("GTUDDTYC", 0x30), ("GTIOR", 0x34),
        ("GTST", 0x38), ("GTCNT", 0x48), ("GTPR", 0x64), ("GTSSR", 0x70),
        ("GTRSR", 0x74), ("GTICASR", 0xB0)]

tag = sys.argv[1] if len(sys.argv) > 1 else "fw"
with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    time.sleep(0.2)
    print(f"== GPT7 dump [{tag}] ==")
    for name, off in REGS:
        try:
            v = t.read32(GPT7 + off)
            print(f"  {name:9s} @+{off:#04x} = {v:#010x}")
        except Exception as e:
            print(f"  {name:9s} read failed: {e}")
    c1 = t.read32(GPT7 + 0x48)
    time.sleep(0.01)
    c2 = t.read32(GPT7 + 0x48)
    print(f"  GTCNT: {c1:#x} -> {c2:#x} {'(counting)' if c1 != c2 else '(STATIC)'}")
    lv = set()
    for _ in range(200):
        try:
            lv.add((t.read32(PFS_P1011) >> 1) & 1)
        except Exception:
            pass
    print(f"  P1011 PIDR samples: {sorted(lv)} => {'PWM present' if len(lv) > 1 else 'PIN STUCK'}")
    w = t.read32(PFS_P1011)
    print(f"  PFS_P1011 = {w:#x} PMR={(w >> 16) & 1} PSEL={(w >> 24) & 0x1F}")

def extra_dump():
    import time as _t
    with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                                 frequency=4000000,
                                                 halt_on_connect=False) as s2:
        t2 = s2.target
        for name, off in (("GTCCRA", 0x4C), ("GTCCRB", 0x50),
                          ("GTADTRA", 0x84), ("GTADTRB", 0x88)):
            v = t2.read32(GPT7 + off)
            print(f"  {name} @+{off:#04x} = {v:#010x}")

if __name__ == "__main__" and len(sys.argv) > 2 and sys.argv[2] == "cc":
    extra_dump()
