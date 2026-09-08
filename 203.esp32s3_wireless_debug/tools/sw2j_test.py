# -*- coding: utf-8 -*-
"""Decisive experiment: is JTAG dead because SWJ-DP is stuck in SWD mode?

Sends SWD->JTAG switch sequence (0xE73E LSB-first, per ARM SWJ-DP spec)
via DAP_SWJ_Sequence (0x12), then reads JTAG IDCODE via DAP_JTAG_IDCODE.
Expected IDCODE for STM32H7 JTAG-DP: 0x6BA00477.
"""
import sys
import time

import pywinusb.hid as hid

VID, PID = 0x303A, 0x1001  # placeholder, overridden by find()


def find_dev():
    for d in hid.find_all_hid_devices():
        if "CMSIS-DAP" in (d.product_name or ""):
            return d
    return None


def hexs(b):
    return " ".join("%02X" % x for x in b)


class Dap:
    def __init__(self, dev):
        self.dev = dev
        self.reports = []
        dev.open()
        dev.set_raw_data_handler(self._cb)
        time.sleep(0.1)

    def _cb(self, data):
        self.reports.append(bytes(data[1:]))

    def cmd(self, payload, wait=0.6):
        # pad to 64-byte HID report
        data = bytes([0x00]) + bytes(payload)
        data = data.ljust(65, b"\x00")
        self.reports.clear()
        self.dev.send_output_report(data)
        t0 = time.time()
        while time.time() - t0 < wait:
            if self.reports:
                r = self.reports[0]
                # response: report-id stripped already; first byte may be len
                return r
            time.sleep(0.01)
        return None

    def close(self):
        self.dev.close()


def ones_bytes(nbits):
    """nbits of 1, LSB-first packing."""
    out = bytearray((nbits + 7) // 8)
    for i in range(nbits):
        out[i // 8] |= 1 << (i % 8)
    return bytes(out)


def swd_to_jtag_seq():
    """51 ones + 0xE73E LSB-first + 51 ones -> bit stream for 0x12."""
    bits = []
    bits += [1] * 51
    magic = 0xE73E
    for i in range(16):
        bits.append((magic >> i) & 1)
    bits += [1] * 51
    data = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b:
            data[i // 8] |= 1 << (i % 8)
    return bytes(data)


def main():
    dev = find_dev()
    if dev is None:
        print("PROBE NOT FOUND")
        return
    print("device:", dev.product_name)
    dap = Dap(dev)

    # 1) baseline: JTAG IDCODE before switch
    dap.cmd([0x02, 0x02])  # connect JTAG
    time.sleep(0.1)
    r = dap.cmd([0x16, 0xFF])
    print("JTAG IDCODE before  :", hexs(r) if r else "NO RESP")

    # 2) send SWD->JTAG sequence (0x12: count then data bytes)
    seq = swd_to_jtag_seq()
    r = dap.cmd([0x12, len(seq)] + list(seq))
    print("SWJ_Sequence SW2J   :", hexs(r) if r else "NO RESP")
    time.sleep(0.05)

    # 3) TLR (5+ TMS=1 cycles) via JTAG_Sequence
    r = dap.cmd([0x14, 0x01, 0x40, 0xFF])  # 8 bits TMS=1
    print("JTAG_Sequence TLR   :", hexs(r) if r else "NO RESP")

    # 4) retry JTAG IDCODE
    for _ in range(3):
        r = dap.cmd([0x16, 0xFF])
        print("JTAG IDCODE after   :", hexs(r) if r else "NO RESP")
        time.sleep(0.05)

    # 5) sanity: switch back to SWD and read DPIDR
    j2s = bytes([0x9E, 0xE7])
    pre = ones_bytes(51)
    post = ones_bytes(51)
    seq2 = pre + j2s + post
    dap.cmd([0x12, len(seq2)] + list(seq2))
    dap.cmd([0x02, 0x01])  # connect SWD
    time.sleep(0.1)
    r = dap.cmd([0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
    print("SWD DPIDR after J2S :", hexs(r) if r else "NO RESP")

    dap.cmd([0x03])  # disconnect
    dap.close()


if __name__ == "__main__":
    main()
