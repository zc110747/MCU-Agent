#!/usr/bin/env python3
"""Correct-address read of ICU.IELSR[] and DSI state on the RA8D1.

R_ICU.IELSR[96] sits at offset 0x6300 inside the ICU block (R7FA8D1BH.h:10528),
NOT at 0x0000 - the earlier probe read the wrong window and got all zeros.

Verified slot map (our gen/vector_data.c):
    slot 0 = SCI9 RXI            -> ELC 0x163
    slot 1 = GLCDC LINE DETECT   -> ELC 0x1CD   (CEU compiled out)
    slot 2 = MIPI DSI SEQ0       -> ELC 0x1D3
    slot 3 = MIPI DSI SEQ1       -> ELC 0x1D4
    slot 4 = MIPI DSI VIN1       -> ELC 0x1D5
    slot 5 = MIPI DSI RCV        -> ELC 0x1D6
    slot 6 = MIPI DSI FERR       -> ELC 0x1D7
    slot 7 = MIPI DSI PPI        -> ELC 0x1D8
"""
import sys
from pyocd.core.session import Session
from pyocd.core.memory_map import MemoryMap, RamRegion

ICU_BASE      = 0x40006000
IELSR_OFF     = 0x6300
DSILINK_BASE  = 0x40346000
LINKSR_OFF    = 0x0010
SQCH0SR_OFF   = 0x05D0
SQCH0IER_OFF  = 0x05D8
RXIER_OFF     = 0x0208
VMIER_OFF     = 0x0210
GLCDC_BASE    = 0x40342000
GLCDC_SYSCNT  = 0x0000
NVIC_ISER0    = 0xE000E100

ELC = {
    0x163: "SCI9_RXI",
    0x1CD: "GLCDC_LINE_DETECT",
    0x1D3: "MIPI_DSI_SEQ0",
    0x1D4: "MIPI_DSI_SEQ1",
    0x1D5: "MIPI_DSI_VIN1",
    0x1D6: "MIPI_DSI_RCV",
    0x1D7: "MIPI_DSI_FERR",
    0x1D8: "MIPI_DSI_PPI",
}
SLOT = {
    0: "SCI9_RXI  (expect 0x163)",
    1: "GLCDC_LD  (expect 0x1CD)",
    2: "DSI_SEQ0   (expect 0x1D3)",
    3: "DSI_SEQ1   (expect 0x1D4)",
    4: "DSI_VIN1   (expect 0x1D5)",
    5: "DSI_RCV    (expect 0x1D6)",
    6: "DSI_FERR   (expect 0x1D7)",
    7: "DSI_PPI    (expect 0x1D8)",
}


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
    mm.add_region(RamRegion(start=0x68000000, length=0x1000000, name="sdram"))
    t.memory_map = mm
    return s, t


def main():
    s, t = open_session()
    try:
        print("\n=== ICU.IELSR[0..11] @ 0x40006000+0x6300 ===")
        ok = 0
        for i in range(12):
            v = t.read32(ICU_BASE + IELSR_OFF + 4 * i)
            iels = v & 0x1FF
            tag = ELC.get(iels, "")
            slot = SLOT.get(i, "")
            status = ""
            if i < 8:
                if iels:
                    status = "ARMED"
                    ok += 1
                else:
                    status = "EMPTY"
            print(f"  IELSR[{i:2d}] = 0x{v:08X}  IELS=0x{iels:03X} {tag:18s} "
                  f"{slot:26s} {status}")
        print(f"\n  armed slots: {ok}/8 ({'YES' if ok >= 8 else 'NO'})")

        print("\n=== NVIC ISER0 ===")
        iser = t.read32(NVIC_ISER0)
        print(f"  0x{iser:08X}")
        for i in range(8):
            print(f"    irq{i} {'enabled' if (iser >> i) & 1 else 'disabled'}")

        print("\n=== DSILINK ===")
        linksr = t.read32(DSILINK_BASE + LINKSR_OFF)
        print(f"  LINKSR   = 0x{linksr:08X}  (SQ0RUN={linksr & 1} VRUN={(linksr >> 8) & 1})")
        print(f"  SQCH0IER = 0x{t.read32(DSILINK_BASE + SQCH0IER_OFF):08X}")
        print(f"  SQCH0SR  = 0x{t.read32(DSILINK_BASE + SQCH0SR_OFF):08X}")
        print(f"  RXIER    = 0x{t.read32(DSILINK_BASE + RXIER_OFF):08X}")
        print(f"  VMIER    = 0x{t.read32(DSILINK_BASE + VMIER_OFF):08X}")

        print("\n=== GLCDC ===")
        syscnt = t.read32(GLCDC_BASE + GLCDC_SYSCNT)
        print(f"  SYSCNT   = 0x{syscnt:08X}  panel_clk_en={syscnt & 1}")
    finally:
        s.close()


if __name__ == "__main__":
    main()
