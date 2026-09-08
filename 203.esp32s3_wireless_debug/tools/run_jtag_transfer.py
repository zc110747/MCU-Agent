#!/usr/bin/env python3
"""Decisive DAP_JTAG_Transfer (0x17) diagnostic over HID.

Directly exercises a real JTAG-DP DPACC transaction (bypassing OpenOCD) to
isolate whether the firmware's jtag_transfer() works for DP/AP register access.

Steps:
  1. Connect(JTAG)                0x02
  2. JTAG_Configure(2 TAPs)       0x15  [irlen cpu=4, bs=5]
  3. JTAG_IDCODE per TAP          0x16
  4. JTAG_Transfer: write ABORT   0x17 (DPACC write addr0, data=0x0000001E)
  5. JTAG_Transfer: read DPIDR    0x17 (DPACC read  addr0) -> expect 0x6BA00477, ack=OK
  6. JTAG_Transfer: read CTRLSTAT 0x17 (DPACC read  addr1)  -> sanity
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

    def cmd(self, p, timeout=2.0):
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

    r = d.cmd([0x02, 0x02])          # Connect JTAG
    print("Connect JTAG     :", hexs(r))
    time.sleep(0.8)

    r = d.cmd([0x15, 0x02, 0x04, 0x05])   # JTAG_Configure: 2 TAPs, cpu ir=4, bs ir=5
    print("JTAG_Configure    :", hexs(r))

    for i in range(2):
        r = d.cmd([0x16, i])
        print("JTAG_IDCODE %d   :" % i, hexs(r))

    # Write ABORT (DPACC addr0, RnW=0 -> 0x00), data 0x0000001E
    r = d.cmd([0x17, 0x00, 0x01, 0x00, 0x1E, 0x00, 0x00, 0x00])
    print("JTAG_Xfer ABORT  :", hexs(r), " ack=%s" % (r[2] if r else '?'))

    # Read DPIDR (DPACC addr0, RnW=1 -> 0x02)
    r = d.cmd([0x17, 0x00, 0x01, 0x02])
    print("JTAG_Xfer DPIDR  :", hexs(r), " ack=%s" % (r[2] if r else '?'))
    if r and len(r) >= 7:
        val = (r[3] | (r[4] << 8) | (r[5] << 16) | (r[6] << 24))
        print("   DPIDR = 0x%08X" % val)

    # Read CTRL/STAT (DPACC addr1, RnW=1 -> 0x04? addr bits: A2=0,A3=1 -> 0x08? wait)
    # request byte: bit1=RnW, bit2=A2, bit3=A3. addr1 = A[3:2]=01 -> A2=1,A3=0 -> bits: RnW=1,A2=1,A3=0 -> 0x02|0x04 = 0x06
    r = d.cmd([0x17, 0x00, 0x01, 0x06])
    print("JTAG_Xfer CTRLST :", hexs(r), " ack=%s" % (r[2] if r else '?'))
    if r and len(r) >= 7:
        val = (r[3] | (r[4] << 8) | (r[5] << 16) | (r[6] << 24))
        print("   CTRL/STAT = 0x%08X" % val)

    d.cmd([0x03])
    d.close()
    print("DONE")


if __name__ == "__main__":
    sys.exit(main() or 0)
