#!/usr/bin/env python3
"""TDO idle-level timeline around nRESET pulse.

Decisive target-liveness test:
  - target powered + JTAG TAP alive -> TDO driven LOW at idle
  - target unpowered / TDO wire dead -> TDO floats HIGH (probe pull-up)
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

    def pins(self):
        r = self.cmd([0x10, 0x00, 0x00])
        return r[1] if r else None

    def close(self):
        self.dev.close()


def main():
    dev = find_dev()
    if dev is None:
        print("PROBE NOT FOUND")
        return 2
    dap = Dap(dev)
    print("device:", dev.product_name)

    # connect JTAG port so engines are active
    dap.cmd([0x02, 0x02])
    time.sleep(0.2)

    def sample(tag, n=8, gap=0.05):
        vals = []
        for _ in range(n):
            p = dap.pins()
            if p is None:
                break
            vals.append((p >> 3) & 1)          # TDO bit
            time.sleep(gap)
        print("%-28s TDO: %s" % (tag, "".join(str(v) for v in vals)))

    sample("before reset")
    # nRESET assert low (mask bit7, value bit7=0)
    dap.cmd([0x11, 0x00, 0x80])
    time.sleep(0.3)
    sample("during reset (NRST low)")
    # release
    dap.cmd([0x11, 0x80, 0x80])
    for i in range(10):
        p = dap.pins()
        if p is None:
            break
        print("  t=%4dms after release: TDO=%d nRESET=%d"
              % (i * 50, (p >> 3) & 1, (p >> 7) & 1))
        time.sleep(0.05)
    sample("after release")

    # try IDCODE right after fresh reset release
    r = dap.cmd([0x16, 0x00])
    print("DAP_JTAG_IDCODE      :", " ".join("%02X" % b for b in r[:6]) if r else "NO RESP")

    dap.cmd([0x03])
    dap.close()
    print("DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
