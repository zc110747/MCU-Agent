#!/usr/bin/env python3
"""Decisive JTAG check over HID (no UART).

Distinguishes three failure modes after the swd_swd_to_jtag() fix:
  1. Switch not effective  -> after Connect(JTAG) SWD DPIDR still answers
  2. TDO physically floating -> SWJ_Pins TDO bit stays 1 (target not driving)
  3. Switch + wiring OK     -> JTAG_IDCODE returns 0x6BA00477

Sequence mirrored from dap_connect(JTAG) in cmsis_dap.c:
  Connect(JTAG) internally does swd_set_idle -> swd_line_reset ->
  swd_swd_to_jtag(0xE73E) -> swd_line_reset -> jtag_connect.
"""
import sys
import time

import pywinusb.hid as hid

VID, PID = 0x303A, 0x8502


def find_dev():
    for d in hid.find_all_hid_devices():
        if d.vendor_id == VID and d.product_id == PID:
            return d
    return None


class Dap:
    def __init__(self, dev):
        self.dev = dev
        self.resp = None
        dev.set_raw_data_handler(self._on)
        dev.open()
        time.sleep(0.2)
        self.out = dev.find_output_reports()[0]

    def _on(self, data):
        self.resp = list(data)[1:]

    def cmd(self, p, timeout=1.0):
        self.resp = None
        buf = [0x00] + list(p) + [0x00] * (65 - 1 - len(p))
        self.out.send(buf)
        t0 = time.time()
        while self.resp is None and time.time() - t0 < timeout:
            time.sleep(0.002)
        return self.resp

    def close(self):
        self.dev.close()


def hexs(b):
    return " ".join("%02X" % x for x in (b or []))


def main():
    dev = find_dev()
    if dev is None:
        print("PROBE NOT FOUND (VID=%04X PID=%04X)" % (VID, PID))
        return 2
    d = Dap(dev)
    print("device:", dev.product_name)

    # --- pins BEFORE connect (pure GPIO read, independent of DP mode) ---
    r = d.cmd([0x10, 0x00, 0x00])
    tdo0 = (r[1] >> 3) & 1 if r else -1
    print("SWJ_Pins before  :", hexs(r), " TDO=%s" % tdo0)

    # --- Connect(JTAG): triggers swd_swd_to_jtag() in firmware ---
    r = d.cmd([0x02, 0x02])
    print("Connect JTAG     :", hexs(r))
    time.sleep(0.8)

    # --- pins AFTER connect: if DP switched to JTAG, PB3(TDO) gets driven ---
    r = d.cmd([0x10, 0x00, 0x00])
    tdo1 = (r[1] >> 3) & 1 if r else -1
    print("SWJ_Pins after   :", hexs(r), " TDO=%s" % tdo1)

    # --- high-level JTAG_IDCODE (Keil path) ---
    for i in range(2):
        r = d.cmd([0x16, 0x00])
        print("JTAG_IDCODE %d   :" % i, hexs(r))

    # --- raw 0x14 scan: TLR then 32-bit DR capture with TDI=0 ---
    # TLR: TMS=1 for 6 bits, no TDO capture
    r = d.cmd([0x14, 0x01, 0x40, 0xFF])
    print("JTAG_Seq TLR     :", hexs(r))
    # DR scan 32 bits, TDI=0, capture TDO
    r = d.cmd([0x14, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00])
    print("JTAG_Seq DR32    :", hexs(r))

    # --- does SWD still answer? (if switch worked, SWD DPIDR must FAIL) ---
    r = d.cmd([0x02, 0x01])               # reconnect SWD
    print("Connect SWD      :", hexs(r))
    time.sleep(0.4)
    d.cmd([0x12, 60] + [0xFF] * 8)        # TLR
    d.cmd([0x12, 16, 0x9E, 0xE7])         # J2S
    d.cmd([0x12, 60] + [0xFF] * 8)        # TLR
    # SWD_Transfer: idx=0, count=1, DP read IDCODE (RnW=1 A=0)
    r = d.cmd([0x05, 0x00, 0x01, 0x02 | 0x04, 0, 0, 0, 0, 0, 0, 0])
    ack = (r[2] >> 0) & 0x07 if r and len(r) > 2 else -1
    print("SWD DPIDR ack    :", hexs(r), " ack=%s" % ack)

    d.cmd([0x03])
    d.close()
    print("DONE")


if __name__ == "__main__":
    sys.exit(main() or 0)
