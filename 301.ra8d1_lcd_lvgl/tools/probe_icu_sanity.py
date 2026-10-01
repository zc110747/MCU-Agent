#!/usr/bin/env python3
"""Sanity-check the SWD probe itself before trusting any ICU reading.

If the shell works over SCI9 then IELSR[0] MUST be non-zero.  Our earlier
read returned 0 for every slot, so either
  (a) the ICU is not where we think it is, or
  (b) our reads are not landing.

This script:
  1. reads a known RAM value and writes/reads it back (proves read+write work)
  2. scans the ICU block in 16-byte steps looking for ANY non-zero word
  3. reads the NVIC priority registers, which R_BSP_IrqCfg() definitely wrote
     if the SCI/GLCDC drivers ran
"""
import sys
from pyocd.core.session import Session
from pyocd.core.memory_map import MemoryMap, RamRegion

ICU_BASE = 0x40006000
NVIC_IPR = 0xE000E400
SCRATCH = 0x220DF000  # top of the 0xE0000-byte RAM region


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
    mm.add_region(RamRegion(start=0x20000000, length=0x200000, name="sram"))
    mm.add_region(RamRegion(start=0x22000000, length=0x200000, name="sram1"))
    mm.add_region(RamRegion(start=0x68000000, length=0x1000000, name="sdram"))
    t.memory_map = mm
    return s, t


def main():
    s, t = open_session()
    try:
        print("\n[1] read/write sanity on SRAM")
        t.write32(SCRATCH, 0xDEADBEEF)
        back = t.read32(SCRATCH)
        print(f"    wrote 0xDEADBEEF, read 0x{back:08X}  "
              f"{'OK' if back == 0xDEADBEEF else 'FAIL'}")

        print("\n[2] scan ICU block (0x40006000, 0x100 words)")
        hits = 0
        for i in range(0, 0x100):
            v = t.read32(ICU_BASE + 4 * i)
            if v != 0:
                hits += 1
                if hits <= 15:
                    print(f"    [+0x{4*i:04X}] 0x{v:08X}")
        print(f"    non-zero words: {hits}")

        print("\n[3] NVIC IPR[0..9] (R_BSP_IrqCfg writes these)")
        for i in range(10):
            v = t.read8(NVIC_IPR + i)
            print(f"    IPR[{i}] = 0x{v:02X}")

        print("\n[4] NVIC ISER")
        print(f"    ISER0 = 0x{t.read32(0xE000E100):08X}")

        print("\n[5] re-read IELSR[0..9] at 0x40006000")
        for i in range(10):
            print(f"    IELSR[{i}] = 0x{t.read32(ICU_BASE + 4*i):08X}")

        print("\n[6] try NS-offset base 0x50006000")
        for i in range(10):
            print(f"    IELSR[{i}] = 0x{t.read32(0x50006000 + 4*i):08X}")
    finally:
        s.close()


if __name__ == "__main__":
    main()
