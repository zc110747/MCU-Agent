#!/usr/bin/env python3
"""Bit-banged I2C probe over SWD (core halted, no firmware involvement).

Drives P408 (SCL) / P409 (SDA) as plain GPIO and performs a full I2C
transaction by hand: START, 7-bit address + W, data byte, STOP - checking the
slave ACK after each byte.  This is completely independent of the SCI3 block,
so it separates "SCI3 misconfigured" from "no slave on the bus".

PmnPFS bits: bit0 PODR, bit1 PIDR, bit2 PDR(1=out), bit4 PCR(pull-up),
             bit6 NCODR, bit16 PMR, bits28:24 PSEL
"""
import sys
import time
from pyocd.core.session import Session
from pyocd.probe.aggregator import DebugProbeAggregator

R_PFS = 0x40400800


def pfs(port, pin):
    return R_PFS + port * 0x40 + pin * 4


def get_probe(retries=30, delay=0.5):
    for _ in range(retries):
        ps = DebugProbeAggregator.get_all_connected_probes()
        if ps:
            return ps[0]
        time.sleep(delay)
    return None


class Bb:
    """Bit-bang SCL=P408, SDA=P409 via SWD writes."""
    SCL = pfs(4, 8)
    SDA = pfs(4, 9)
    HOLD = 0.0003   # ~1.6 kHz, very slow and safe

    def __init__(self, t):
        self.t = t

    def w(self, addr, v):
        self.t.write32(addr, v)
        time.sleep(self.HOLD)

    def out_low(self, addr):
        self.w(addr, (1 << 2))       # PDR=1, PODR=0

    def out_high(self, addr):
        self.w(addr, (1 << 2) | 1)   # PDR=1, PODR=1

    def in_pu(self, addr):
        self.w(addr, (1 << 4))       # input + pull-up

    def rd(self, addr):
        return (self.t.read32(addr) >> 1) & 1

    def init(self):
        self.in_pu(self.SCL)
        self.in_pu(self.SDA)
        time.sleep(0.001)

    def start(self):
        self.out_high(self.SDA)
        self.out_high(self.SCL)
        self.out_low(self.SDA)     # SDA falls while SCL high -> START
        self.out_low(self.SCL)

    def stop(self):
        self.out_low(self.SDA)
        self.out_high(self.SCL)
        self.out_high(self.SDA)    # SDA rises while SCL high -> STOP

    def clock(self):
        self.out_high(self.SCL)
        self.out_low(self.SCL)

    def write_bit(self, b):
        if b:
            self.out_high(self.SDA)
        else:
            self.out_low(self.SDA)
        self.clock()

    def read_bit(self):
        self.in_pu(self.SDA)
        self.out_high(self.SCL)
        v = self.rd(self.SDA)
        self.out_low(self.SCL)
        return v

    def write_byte(self, val):
        for i in range(8):
            self.write_bit((val >> (7 - i)) & 1)
        # ACK: master releases SDA, slave pulls low
        self.in_pu(self.SDA)
        self.out_high(self.SCL)
        ack = self.rd(self.SDA)
        self.out_low(self.SCL)
        return ack  # 0 = ACK, 1 = NACK


def main():
    p = get_probe()
    if p is None:
        sys.exit("no probe")
    print("probe:", p.unique_id)
    s = Session(p, target_override="cortex_m", frequency=1000000,
                options={"no_config": True})
    s.open()
    t = s.target
    t.halt()

    b = Bb(t)
    b.init()
    print("idle SCL=%d SDA=%d" % (b.rd(b.SCL), b.rd(b.SDA)))

    addr_w = (0x15 << 1) | 0
    print("\n--- bit-banged START + addr 0x%02X (write) ---" % addr_w)
    b.start()
    ack = b.write_byte(addr_w)
    print("  address byte ACK = %d (%s)" % (ack, "ACK" if ack == 0 else "NACK"))
    if ack == 0:
        ack2 = b.write_byte(0xA9)
        print("  reg 0xA9  ACK = %d (%s)" % (ack2, "ACK" if ack2 == 0 else "NACK"))
    b.stop()
    b.init()

    # Try a few other well-known small addresses to see if anything answers
    print("\n--- scan a few addresses ---")
    for a in (0x14, 0x15, 0x1A, 0x38, 0x5D, 0x70):
        b.start()
        ack = b.write_byte((a << 1) | 0)
        b.stop()
        b.init()
        print("  0x%02X -> %s" % (a, "ACK" if ack == 0 else "NACK"))

    t.resume()
    s.close()


if __name__ == "__main__":
    main()
