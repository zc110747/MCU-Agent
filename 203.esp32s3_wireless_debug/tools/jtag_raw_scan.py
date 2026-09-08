#!/usr/bin/env python3
"""Raw JTAG scan via DAP_JTAG_Sequence (0x14) - validates sequence fix."""
import sys
import time

import pywinusb.hid as hid

VID, PID = 0x303A, 0x8502


class Dap:
    def __init__(self, dev):
        self.dev = dev
        self.resp = None
        dev.set_raw_data_handler(self._on_in)
        dev.open()
        time.sleep(0.2)
        self.out = dev.find_output_reports()[0]

    def _on_in(self, data):
        self.resp = list(data)[1:]

    def cmd(self, payload, timeout=0.5):
        self.resp = None
        buf = [0x00] + list(payload) + [0x00] * (65 - 1 - len(payload))
        self.out.send(buf)
        t0 = time.time()
        while self.resp is None and time.time() - t0 < timeout:
            time.sleep(0.002)
        return self.resp

    def close(self):
        self.dev.close()


def find_dev():
    for d in hid.find_all_hid_devices():
        if d.vendor_id == VID and d.product_id == PID:
            return d
    return None


def main():
    dev = find_dev()
    if dev is None:
        print("PROBE NOT FOUND")
        return 2
    dap = Dap(dev)
    print("device:", dev.product_name)
    r = dap.cmd([0x02, 0x02])
    print("DAP_Connect JTAG    :", " ".join("%02X" % b for b in r[:4]) if r else "NO RESP")
    time.sleep(0.2)

    # seq1: TMS=1 x 6 bits (TLR), no TDO capture. info=0x40
    r = dap.cmd([0x14, 0x01, 0x40, 0xFF])
    print("JTAG_Sequence TLR   :", " ".join("%02X" % b for b in r[:6]) if r else "NO RESP")

    # seq2: 32 TDI=0 bits with TDO capture. info=0x80, count=32 -> 4 bytes out
    r = dap.cmd([0x14, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00])
    print("JTAG_Sequence DR32  :", " ".join("%02X" % b for b in r[:8]) if r else "NO RESP")

    # seq3: IDCODE read proper: TLR again, then Exit1/DR navigation is
    # handled by OpenOCD normally; here just sample 8 idle bits TDO
    r = dap.cmd([0x14, 0x01, 0x80, 0x00])
    print("JTAG_Sequence TDO8  :", " ".join("%02X" % b for b in r[:4]) if r else "NO RESP")

    dap.cmd([0x03])
    dap.close()
    print("DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
