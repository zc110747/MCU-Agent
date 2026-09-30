# -*- coding: utf-8 -*-
"""Send JTAG->SWD (0xE79E) via DAP_SWJ_Sequence to test the 0x12 channel.

DP is currently in SWD mode (from a prior swd_connect). If the 0x12
bit-bang path works, sending J2S while in SWD mode... note: per ADI spec
the DP samples for switch sequences regardless of current mode, so J2S
received -> stays/enters SWD (no visible change). To make the test
decisive we instead send J2S TWICE-adjacent framing is irrelevant; the
real decisive test is SW2J. So this script tries BOTH magics back to
back with DPIDR probes:

  stage A: DPIDR                     (expect OK, SWD alive)
  stage B: SW2J (0xE73E) via 0x12    -> DPIDR (expect FAIL if switch works)
  stage C: J2S  (0xE79E) via 0x12    -> DPIDR (expect OK again if switch works)
  stage D: SW2J (0xE73E) via 0x12, 2 repeats -> DPIDR + JTAG IDCODE
"""
import time

import pywinusb.hid as hid


def find_dev():
    for d in hid.find_all_hid_devices():
        if "CMSIS-DAP" in (d.product_name or ""):
            return d
    return None


def hexs(b):
    return " ".join("%02X" % x for x in b[:8])


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
        data = bytes([0x00]) + bytes(payload)
        data = data.ljust(65, b"\x00")
        self.reports.clear()
        self.dev.send_output_report(data)
        t0 = time.time()
        while time.time() - t0 < wait:
            if self.reports:
                return self.reports[0]
            time.sleep(0.01)
        return None

    def close(self):
        self.dev.close()


def seq_bytes(magic):
    """51 ones + 16-bit magic LSB-first + 51 ones, LSB-first packed."""
    bits = [1] * 51
    for i in range(16):
        bits.append((magic >> i) & 1)
    bits += [1] * 51
    data = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b:
            data[i // 8] |= 1 << (i % 8)
    return bytes(data)


def swj_seq(dap, bits, data):
    return dap.cmd([0x12, bits] + list(data))


def dpidr(dap):
    r = dap.cmd([0x05, 0x00, 0x01, 0xA5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
    return hexs(r) if r else "NO RESP"


def main():
    dev = find_dev()
    if dev is None:
        print("PROBE NOT FOUND")
        return
    dap = Dap(dev)
    print("device:", dev.product_name)

    dap.cmd([0x02, 0x01])  # connect SWD (firmware sends J2S internally)
    time.sleep(0.2)
    print("A) DPIDR           :", dpidr(dap), "(expect OK)")

    sw2j = seq_bytes(0xE73E)
    r = swj_seq(dap, len(sw2j) * 8, sw2j)
    print("B) SW2J via 0x12   :", hexs(r) if r else "NO RESP")
    time.sleep(0.05)
    print("   DPIDR           :", dpidr(dap), "(expect FAIL 0x07)")

    j2s = seq_bytes(0xE79E)
    r = swj_seq(dap, len(j2s) * 8, j2s)
    print("C) J2S via 0x12    :", hexs(r) if r else "NO RESP")
    time.sleep(0.05)
    print("   DPIDR           :", dpidr(dap), "(expect OK again)")

    # repeat SW2J 3x then try JTAG IDCODE via full connect
    for _ in range(3):
        swj_seq(dap, len(sw2j) * 8, sw2j)
        time.sleep(0.02)
    dap.cmd([0x02, 0x02])
    time.sleep(0.1)
    dap.cmd([0x14, 0x01, 0x40, 0xFF])  # TLR
    r = dap.cmd([0x16, 0xFF])
    print("D) JTAG IDCODE     :", hexs(r) if r else "NO RESP")

    dap.cmd([0x03])
    dap.close()


if __name__ == "__main__":
    main()
