#!/usr/bin/env python3
"""Authoritative SWD<->JTAG switch test over HID.

OpenOCD already proved SWD works (DPIDR 0x6ba02477), so target power and
the shared TMS/TCK lines are fine. This isolates the JTAG failure:
  - put DP in SWD mode (TDO should float, =1)
  - Connect(JTAG) -> swd_swd_to_jtag() should move DP to JTAG mode
  - if TDO then gets driven (reads 0) AND JTAG_IDCODE returns 0x6ba02477,
    wiring + switch are OK and the OpenOCD JTAG issue is elsewhere
  - if TDO stays floating (1) and JTAG_IDCODE is empty -> TDO wire open or
    switch not effective
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


def tdo(r):
    return (r[1] >> 3) & 1 if r else -1


def swd_line_reset(d):
    d.cmd([0x12, 64] + [0xFF] * 8)          # >=50 idle cycles, SWDIO high
    d.cmd([0x12, 16, 0x9E, 0xE7])            # JTAG->SWD magic 0xE79E
    d.cmd([0x12, 64] + [0xFF] * 8)


def main():
    dev = find_dev()
    if dev is None:
        print("PROBE NOT FOUND")
        return 2
    d = Dap(dev)
    print("device:", dev.product_name)

    # ---- Phase A: force DP into SWD mode ----
    d.cmd([0x02, 0x01])
    time.sleep(0.3)
    swd_line_reset(d)
    r = d.cmd([0x10, 0x00, 0x00])
    print("[A] SWD mode  TDO=%s  (expect 1=floating, SWD does not drive TDO)" % tdo(r))

    # ---- Phase B: switch to JTAG ----
    d.cmd([0x02, 0x02])                       # Connect JTAG -> swd_swd_to_jtag()
    time.sleep(0.8)
    r = d.cmd([0x10, 0x00, 0x00])
    print("[B] JTAG mode TDO=%s  (if switch worked + wire ok, should be driven=0)" % tdo(r))
    for i in range(2):
        r = d.cmd([0x16, 0x00])
        print("    JTAG_IDCODE %d: %s" % (i, hexs(r)))

    # ---- Phase C: switch back to SWD, confirm DP responds to SWD ----
    d.cmd([0x02, 0x01])
    time.sleep(0.3)
    swd_line_reset(d)
    r = d.cmd([0x10, 0x00, 0x00])
    print("[C] back SWD  TDO=%s" % tdo(r))

    d.cmd([0x03])
    d.close()
    print("DONE")


if __name__ == "__main__":
    sys.exit(main() or 0)
