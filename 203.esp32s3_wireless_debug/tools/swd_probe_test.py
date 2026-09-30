#!/usr/bin/env python3
"""PC-side CMSIS-DAP experiment matrix for the ESP32-S3 probe.

Runs over HID (no UART needed). Distinguishes:
  - probe engine health  (DAP_JTAG_IDCODE with live target)
  - target JTAG TAP state (DAP_JTAG_Sequence TDO idle level)
  - SWD ack behavior       (DAP_Transfer DPIDR reads, connect-under-reset)

Usage:  python swd_probe_test.py
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
        self.resp = list(data)[1:]          # strip report id

    def cmd(self, payload, timeout=0.5):
        """payload: list of bytes WITHOUT report id (cmd first)."""
        self.resp = None
        buf = [0x00] + list(payload) + [0x00] * (65 - 1 - len(payload))
        self.out.send(buf)
        t0 = time.time()
        while self.resp is None and time.time() - t0 < timeout:
            time.sleep(0.002)
        return self.resp

    def close(self):
        self.dev.close()


def hexs(b):
    return " ".join("%02X" % x for x in b[:12])


def main():
    dev = find_dev()
    if dev is None:
        print("PROBE NOT FOUND (VID=%04X PID=%04X) - plug the native USB"
              % (VID, PID))
        return 2
    dap = Dap(dev)
    print("device:", dev.product_name)

    # --- DAP_Info capabilities ---
    r = dap.cmd([0x00, 0xF0])
    print("DAP_Info caps      :", hexs(r) if r else "NO RESP")

    # --- 1. JTAG: connect + IDCODE via high-level command (Keil path) ---
    r = dap.cmd([0x02, 0x02])               # DAP_Connect, JTAG
    print("DAP_Connect JTAG   :", hexs(r) if r else "NO RESP")
    time.sleep(0.3)
    for _ in range(3):
        r = dap.cmd([0x16, 0x00])           # DAP_JTAG_IDCODE tap0
        print("DAP_JTAG_IDCODE    :", hexs(r) if r else "NO RESP")
        time.sleep(0.1)

    # --- 2. JTAG TDO idle level via SWJ_Pins (bit3) ---
    r = dap.cmd([0x10, 0x00, 0x00])         # readback, no drive
    if r:
        pins = r[1]
        print("SWJ_Pins readback  : 0x%02X (TDO=%d nTRST=%d nRESET=%d)"
              % (pins, (pins >> 3) & 1, (pins >> 5) & 1, (pins >> 7) & 1))

    # --- 3. SWD: reconnect port=1 then DPIDR reads ---
    r = dap.cmd([0x02, 0x01])               # DAP_Connect, SWD
    print("DAP_Connect SWD    :", hexs(r) if r else "NO RESP")
    time.sleep(0.3)
    # host-side line reset + J2S + line reset (same as swd_connect)
    dap.cmd([0x12, 60] + [0xFF] * 8)        # SWJ_Sequence TLR
    dap.cmd([0x12, 16, 0x9E, 0xE7])         # SWJ_Sequence J2S magic
    dap.cmd([0x12, 60] + [0xFF] * 8)        # SWJ_Sequence TLR
    for i in range(3):
        # DAP_Transfer: index=0, count=1, DP read IDCODE (RnW=1, A=0x00)
        r = dap.cmd([0x05, 0x00, 0x01, 0x02 | 0x04, 0, 0, 0])
        print("DAP_Transfer DPIDR :", hexs(r) if r else "NO RESP")
        time.sleep(0.1)

    # --- 4. connect-under-reset: nRESET low -> J2S -> DPIDR -> release ---
    print("--- connect under reset ---")
    dap.cmd([0x11, 0x00, 0x80])             # SWJ_Pins: nRESET low (mask 0x80)
    time.sleep(0.05)
    dap.cmd([0x12, 16, 0x9E, 0xE7])         # J2S while in reset
    dap.cmd([0x11, 0x80, 0x80])             # release nRESET
    time.sleep(0.05)
    dap.cmd([0x12, 60] + [0xFF] * 8)        # TLR
    r = dap.cmd([0x05, 0x00, 0x01, 0x02 | 0x04, 0, 0, 0])
    print("DPIDR under reset  :", hexs(r) if r else "NO RESP")

    # --- 5. JTAG disconnect + SWD disconnect ---
    dap.cmd([0x03])
    dap.close()
    print("DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())


def jtag_raw_scan():
    """Raw JTAG: TLR then 64-bit DR scan with TDO capture, via 0x14."""
    dev = find_dev()
    if dev is None:
        print("PROBE NOT FOUND")
        return
    dap = Dap(dev)
    dap.cmd([0x02, 0x02])                          # connect JTAG
    time.sleep(0.2)
    # one sequence: TMS=1 for 6 bits (TLR), TDO capture off
    # request: [count][info, data]...  info = 0x40 (TMS) | 0x00 (no TDO)
    r = dap.cmd([0x14, 0x01, 0x40 | 0x00, 0xFF])
    print("JTAG_Sequence TLR   :", hexs(r) if r else "NO RESP")
    # DR scan: leave TMS low, clock 32 bits TDI=0, capture TDO
    # info = 0x80 (TDO capture) | 0x00 (no TMS), 32 bits = 4 bytes
    r = dap.cmd([0x14, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00])
    print("JTAG_Sequence DR32  :", hexs(r) if r else "NO RESP")
    dap.cmd([0x03])
    dap.close()


if __name__ == "__main2__":
    pass
