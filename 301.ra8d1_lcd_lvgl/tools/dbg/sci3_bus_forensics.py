#!/usr/bin/env python3
"""SWD bus-state forensics for the CST812T I2C bus (SCI3 on P408/P409).

Answers three questions with the core halted:
  1. What are the raw line levels of SCL/SDA when muxed as plain GPIO?
     (both high = healthy idle; SCL low = a slave/firmware is holding it)
  2. Can we recover the bus with the standard 9-clock trick + STOP?
  3. After recovery, does the CST812T at 0x15 ACK its address?

Run with the firmware halted. Everything is done over SWD; the firmware is
resumed at the end.
"""
import sys
import time
from pyocd.core.session import Session
from pyocd.probe.aggregator import DebugProbeAggregator

SCI3 = 0x40358300
R_PFS_BASE = 0x40400800

RDR, TDR, CCR0 = 0x00, 0x04, 0x08
CESR, ICR, CSR, ISR, CFCLR, ICFCLR = 0x1C, 0x20, 0x48, 0x4C, 0x68, 0x6C

IICINTM, IICCSC, IICACKT = 1 << 8, 1 << 9, 1 << 13
IICSTAREQ, IICRSTAREQ, IICSTPREQ = 1 << 16, 1 << 17, 1 << 18
IICSDAS, IICSCLS = 1 << 20, 1 << 22
CCR0_TE, CCR0_RE = 1 << 4, 1 << 0
ISR_IICACKR, ISR_IICSTIF = 1 << 0, 1 << 3


def pfs_addr(port, pin):
    return R_PFS_BASE + port * 0x40 + pin * 4


def get_probe(retries=25, delay=0.5):
    for _ in range(retries):
        ps = DebugProbeAggregator.get_all_connected_probes()
        if ps:
            return ps[0]
        time.sleep(delay)
    return None


class Board:
    def __init__(self, t):
        self.t = t
        self.saved = None

    def r32(self, a):
        return self.t.read32(a)

    def w32(self, a, v):
        self.t.write32(a, v)

    def sci(self, off):
        return self.r32(SCI3 + off)

    def sci_w(self, off, v):
        self.w32(SCI3 + off, v)

    # ---- GPIO view of the two bus lines ---------------------------------
    def save_mux(self):
        self.saved = (self.r32(pfs_addr(4, 8)), self.r32(pfs_addr(4, 9)))

    def restore_mux(self):
        if self.saved:
            self.w32(pfs_addr(4, 8), self.saved[0])
            self.w32(pfs_addr(4, 9), self.saved[1])

    def gpio_levels(self):
        # PMR=0,PDR=0,PCR=0 -> pure input, read PIDR
        self.w32(pfs_addr(4, 8), 0x0)
        self.w32(pfs_addr(4, 9), 0x0)
        scl = (self.r32(pfs_addr(4, 8)) >> 1) & 1
        sda = (self.r32(pfs_addr(4, 9)) >> 1) & 1
        return scl, sda

    def drive(self, scl, sda):
        # PDR=1 output, PODR=level ; PCR=1 pull-up enable when input
        v408 = (1 << 2) | (scl & 1)
        v409 = (1 << 2) | (sda & 1)
        self.w32(pfs_addr(4, 8), v408)
        self.w32(pfs_addr(4, 9), v409)

    def release(self):
        # input with pull-up
        self.w32(pfs_addr(4, 8), (1 << 4))
        self.w32(pfs_addr(4, 9), (1 << 4))

    # ---- SCI_B manual primitives ----------------------------------------
    def sci_enable(self):
        self.sci_w(CCR0, CCR0_TE | CCR0_RE)
        self.sci_w(ICFCLR, 0xFFFFFFFF)
        self.sci_w(CFCLR, 0xFFFFFFFF)

    def sci_icr_base(self):
        return (31 << 0) | IICSDAS | IICSCLS

    def sci_issue(self, bit):
        icr = self.sci_icr_base()
        self.sci_w(ICR, icr | bit)
        for _ in range(500):
            v = self.sci(ISR)
            if v & ISR_IICSTIF:
                return True, v
        return False, self.sci(ISR)

    def sci_stop(self):
        ok, _ = self.sci_issue(IICSTPREQ)
        self.sci_w(ICFCLR, ISR_IICSTIF)
        return ok


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
    b = Board(t)

    # ---- 1. per-line levels, mux first, then as GPIO --------------------
    print("\n=== 1. Bus line levels (P408=SCL, P409=SDA) ===")
    b.save_mux()
    print(f"  PFS: P408=0x{b.r32(pfs_addr(4,8)):08X} P409=0x{b.r32(pfs_addr(4,9)):08X}")
    scl, sda = b.gpio_levels()
    print(f"  as GPIO input: SCL(P408)={scl}  SDA(P409)={sda}   "
          f"({'both HIGH = idle OK' if (scl and sda) else 'LINE HELD LOW = stuck bus'})")

    # ---- 2. 9-clock recovery on the GPIOs -------------------------------
    print("\n=== 2. 9-clock bus recovery (bit-banged on GPIO) ===")
    for i in range(9):
        b.drive(0, 0)
        time.sleep(0.0001)
        b.drive(1, 0)
        time.sleep(0.0001)
    # STOP: SDA low->high while SCL high
    b.drive(0, 0)
    b.drive(1, 0)
    b.drive(1, 1)
    b.release()
    time.sleep(0.002)
    scl, sda = b.gpio_levels()
    print(f"  after recovery: SCL={scl} SDA={sda} "
          f"({'idle OK' if (scl and sda) else 'still stuck'})")

    # ---- 3. SCI3 address probe after recovery ---------------------------
    print("\n=== 3. SCI3 address probe (slave 0x15) after recovery ===")
    b.restore_mux()
    b.sci_enable()
    print(f"  CCR0=0x{b.sci(CCR0):08X} ISR=0x{b.sci(ISR):08X}")
    ok, _ = b.sci_issue(IICSTAREQ)
    print(f"  START issued: {ok}")
    b.sci_w(ICFCLR, ISR_IICSTIF)
    b.sci_w(TDR, (0x15 << 1) | 0)
    for _ in range(500):
        if b.sci(ISR) & ISR_IICACKR:
            break
    isr = b.sci(ISR)
    print(f"  addr 0x2A: ISR=0x{isr:08X} -> {'NACK' if (isr & ISR_IICACKR) else 'ACK'}")
    b.sci_stop()
    b.sci_w(ICFCLR, 0xFFFFFFFF)

    b.restore_mux()
    t.resume()
    session.close()


if __name__ == "__main__":
    main()
