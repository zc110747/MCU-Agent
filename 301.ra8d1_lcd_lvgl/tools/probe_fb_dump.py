#!/usr/bin/env python3
"""Dump the GLCDC framebuffer so we can tell apart two failure modes:

  A) the framebuffer itself holds garbage  -> fill/lvgl is broken
  B) the framebuffer holds good pixels but the panel shows noise
     -> DSI video mapping / panel init is broken

Also reports the GLCDC BG/GR layer setup so we know which layer is on top
and whether the background plane is black or something else.
"""
import sys
from pyocd.core.session import Session
from pyocd.core.memory_map import MemoryMap, RamRegion

FB0 = 0x68000000          # from `lcd info`
FB1 = 0x68054600
FB_SIZE = 480 * 360 * 2
GLCDC_BASE = 0x40342000


def open_session(frequency=4_000_000):
    from pyocd.probe.aggregator import DebugProbeAggregator
    probes = DebugProbeAggregator.get_all_connected_probes()
    if not probes:
        sys.exit("no probe")
    probe = probes[0]
    print(f"probe: {probe.description} [{probe.unique_id}]")
    s = Session(probe, target_override="cortex_m", frequency=frequency,
                options={"no_config": True})
    s.open()
    t = s.target
    mm = MemoryMap()
    mm.add_region(RamRegion(start=0x22000000, length=0x200000, name="sram"))
    mm.add_region(RamRegion(start=0x68000000, length=0x8000000, name="sdram"))
    t.memory_map = mm
    return s, t


def histogram(target, addr, nbytes, step):
    from collections import Counter
    c = Counter()
    for off in range(0, nbytes, step):
        c[target.read16(addr + off)] += 1
    return c


def main():
    s, t = open_session()
    try:
        print(f"\n=== framebuffer 0x{FB0:08X} first 32 halfwords ===")
        for row in range(4):
            vals = [t.read16(FB0 + 2 * (row * 8 + i)) for i in range(8)]
            print("  " + " ".join(f"{v:04X}" for v in vals))

        print("\n=== colour histogram, stride 0x400 (first 512 samples) ===")
        c = histogram(t, FB0, FB_SIZE, 0x400)
        for v, n in c.most_common(8):
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            print(f"  0x{v:04X} (r{r:02d} g{g:02d} b{b:02d})  x{n}")

        total = sum(c.values())
        top = c.most_common(1)[0]
        print(f"\n  samples={total}  unique={len(c)}")
        if top[1] > total * 0.9:
            print(f"  -> framebuffer is FLAT 0x{top[0]:04X} (uniform fill)")
        elif len(c) > 20:
            print("  -> framebuffer has varied content (UI/text/pattern)")
        else:
            print("  -> framebuffer has few distinct colours")

        print("\n=== GLCDC register snapshot ===")
        # SYSCNT, BG planes, GR1
        for name, off in [("SYSCNT", 0x00), ("BG.EN", 0x10), ("BG.PERI", 0x14),
                          ("BG.SYNC", 0x18), ("BG.VSIZE", 0x1C), ("BG.HSIZE", 0x20),
                          ("BG.BGC", 0x24), ("GR1.EN", 0x50), ("GR1.FBA", 0x54)]:
            try:
                print(f"  {name:10s} [+0x{off:03X}] = 0x{t.read32(GLCDC_BASE + off):08X}")
            except Exception as e:
                print(f"  {name:10s} read failed: {e}")
    finally:
        s.close()


if __name__ == "__main__":
    main()
