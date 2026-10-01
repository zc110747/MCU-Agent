#!/usr/bin/env python3
"""SWD live inspection of the CST812T I2C path on RA8D1.

Connects via pyocd (ART-Link), halts the core, and reads the registers that
decide whether the SCI3 I2C transfer can ever succeed:

  - NVIC ISER0           (0xE000E100)     bit8/9 = SCI3 TXI/TEI enable
  - ICU IELSR[8]/[9]     (0x4000C320/324) ELC event mapped to IRQ8/IRQ9
  - ICU IELSR[0..9]      (0x4000C300)     full non-fixed window
  - SCI3 block           (0x40358300)     CCR0/ICR/ISR/...
  - PORT4 PFS P408/P409  (0x4040_0800)    actual pin mux (PSEL) for SCL/SDA
  - PORT0 PFS P000/P010  (0x4040_0000)    RST/INT pin mux
  - g_touch_event / ctrl state

Key addresses (verified against bsp_irq.c / R7FA8D1BH.h):
  R_ICU_BASE  = 0x40006000, IELSR[] at +0x6300 -> 0x4000C300
  R_PFS_BASE  = 0x40400800, R_PFS.PORT[15], each port = 16 pins x 4 bytes (0x40)
              so PmnPFS = 0x40400800 + m*0x40 + n*4
"""
import sys
from pyocd.core.session import Session
from pyocd.probe.aggregator import DebugProbeAggregator

SCI3 = 0x40358300
ICU  = 0x40006000
IELSR_BASE = ICU + 0x6300        # 0x4000C300  (R_ICU_Type.IELSR[] offset)
NVIC_ISER0 = 0xE000E100
R_PFS_BASE = 0x40400800          # R_PFS (pin function select), 15 ports x 0x40


def pfs_addr(port, pin):
    return R_PFS_BASE + port * 0x40 + pin * 4


def get_probe(retries=20, delay=0.5):
    """ART-Link CMSIS-DAP enumerates intermittently; retry."""
    import time
    for _ in range(retries):
        probes = DebugProbeAggregator.get_all_connected_probes()
        if probes:
            return probes[0]
        time.sleep(delay)
    return None


def main():
    probe = get_probe()
    if probe is None:
        sys.exit("no probe after retries")
    print("probe:", probe.unique_id)
    session = Session(probe, target_override="cortex_m",
                      frequency=1000000, options={"no_config": True})
    session.open()
    t = session.target
    t.halt()

    def rdmem32(addr, words):
        return t.read_memory_block32(addr, words)

    # ---- NVIC -------------------------------------------------------------
    iser0 = t.read32(NVIC_ISER0)
    print(f"\nNVIC ISER0 = 0x{iser0:08X}")
    print(f"  IRQ8 (SCI3_TXI) enabled: {(iser0 >> 8) & 1}")
    print(f"  IRQ9 (SCI3_TEI) enabled: {(iser0 >> 9) & 1}")

    # ---- IELSR (correct address!) ----------------------------------------
    iels = rdmem32(IELSR_BASE, 10)
    print(f"\nIELSR[0..9] @0x{IELSR_BASE:08X}:")
    for i, v in enumerate(iels):
        mark = ""
        if i == 8:
            mark = "  <- SCI3_TXI expect 0x13A"
        elif i == 9:
            mark = "  <- SCI3_TEI expect 0x13B"
        print(f"  [{i}] = 0x{v:08X}{mark}")

    # ---- SCI3 register block ---------------------------------------------
    # SCI_B R_SCI_B0_Type: RDR=0x00 TDR=0x04 CCR0=0x08 CCR1=0x0C CCR2=0x10
    #                      CCR3=0x14 CCR4=0x18 CESR=0x1C ICR=0x20 FCR=0x24
    #                      ... CSR=0x48 ISR=0x4C ... CFCLR=0x68 ICFCLR=0x6C
    def s32(off):
        return t.read32(SCI3 + off)

    print(f"\nSCI3 @0x{SCI3:08X}:")
    print(f"  CCR0  (@0x08) = 0x{s32(0x08):08X}  TE={(s32(0x08) >> 4) & 1} RE={s32(0x08) & 1}")
    icr = s32(0x20)
    print(f"  ICR   (@0x20) = 0x{icr:08X}  IICSTAREQ={(icr >> 16) & 1} "
          f"IICRSTAREQ={(icr >> 17) & 1} IICSDAS={(icr >> 20) & 3} IICSCLS={(icr >> 22) & 3}")
    isr = s32(0x4C)
    print(f"  ISR   (@0x4C) = 0x{isr:08X}  IICACKR={(isr >> 0) & 1} (1=NACK) IICSTIF={(isr >> 3) & 1}")
    print(f"  CCR1  (@0x0C) = 0x{s32(0x0C):08X}")
    print(f"  CCR2  (@0x10) = 0x{s32(0x10):08X}  (BRR[7:0] MDDR[15:8])")
    print(f"  BRR      = 0x{s32(0x10) & 0xFF:02X}   MDDR = 0x{(s32(0x10) >> 8) & 0xFF:02X}")
    print(f"  CCR3  (@0x14) = 0x{s32(0x14):08X}")
    print(f"  CCR4  (@0x18) = 0x{s32(0x18):08X}")
    print(f"  CESR  (@0x1C) = 0x{s32(0x1C):08X}")
    print(f"  CSR   (@0x48) = 0x{s32(0x48):08X}")

    # ---- PFS for P408/P409 (SCL/SDA) and P000/P010 -----------------------
    #  PmnPFS (R_PFS.PORT[m].PIN[n]) is a 32-bit register whose fields are:
    #    bit0    PODR  output data
    #    bit1    PIDR  input data (read level)
    #    bit2    PDR   direction (1=output)
    #    bit4    PCR   pull-up control
    #    bit6    NCODR N-channel open drain
    #    bit[11:10] DSCR drive strength
    #    bit14   ISEL  IRQ input enable
    #    bit15   ASEL  analog input enable
    #    bit16   PMR   port mode control (1=peripheral)
    #    bit[28:24] PSEL pin function select (5 bits, see MPC table)
    def dump_pfs(port, pin, label):
        v = t.read32(pfs_addr(port, pin))
        podr = (v >> 0) & 1
        pidr = (v >> 1) & 1
        pdr  = (v >> 2) & 1
        pcr  = (v >> 4) & 1
        ncodr = (v >> 6) & 1
        dscr = (v >> 10) & 3
        isel = (v >> 14) & 1
        asel = (v >> 15) & 1
        pmr  = (v >> 16) & 1
        psel = (v >> 24) & 0x1F
        print(f"  P{port}{pin:02d} (@0x{pfs_addr(port, pin):08X}) = 0x{v:08X}  "
              f"PMR={pmr} PSEL={psel} PDR={pdr} PODR={podr} PIDR={pidr} "
              f"PCR={pcr} NCODR={ncodr} DSCR={dscr} ASEL={asel} ISEL={isel}  ({label})")

    print("\nPORT PFS (R_PFS @0x%08X):" % R_PFS_BASE)
    dump_pfs(4, 8, "SCI3 SCL")
    dump_pfs(4, 9, "SCI3 SDA")
    dump_pfs(0, 0, "TOUCH RST")
    dump_pfs(0, 10, "TOUCH INT")
    dump_pfs(2, 8, "SCI9 TXD (console)")

    # ---- driver symbols ---------------------------------------------------
    try:
        ev = t.get_symbol_value("g_touch_event")
        cb = t.get_symbol_value("g_touch_cb_count")
        if ev:
            print(f"\n&g_touch_event   = 0x{ev:X} -> 0x{t.read32(ev):08X}")
        if cb:
            print(f"&g_touch_cb_count= 0x{cb:X} -> {t.read32(cb)}")
    except Exception as ex:
        print("\nsymbol resolve err:", ex)

    t.resume()
    session.close()


if __name__ == "__main__":
    main()
