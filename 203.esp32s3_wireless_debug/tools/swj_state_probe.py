# -*- coding: utf-8 -*-
"""State-machine probe: prove whether SWD->JTAG switch sequence reaches DP.

Reads SWD DPIDR (should succeed while DP is in SWD mode), sends the
SWD->JTAG sequence (0xE73E) via DAP_SWJ_Sequence, reads SWD DPIDR again
(should now FAIL 0x07 if the switch reached the DP), then reads JTAG
IDCODE (should succeed if TDO/TDI wires are OK).
Also samples TDO idle level via DAP_SWJ_Pins at every stage.
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


def ones_bytes(nbits):
    out = bytearray((nbits + 7) // 8)
    for i in range(nbits):
        out[i // 8] |= 1 << (i % 8)
    return bytes(out)


def sw2j_bytes():
    bits = [1] * 51
    for i in range(16):
        bits.append((0xE73E >> i) & 1)
    bits += [1] * 51
    data = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b:
            data[i // 8] |= 1 << (i % 8)
    return bytes(data)


def read_dpidr(dap):
    # [0x05][dap_index=0][count=1][request=0xA5: start1,APnDP0,RnW1,A=00,parity1,stop0,park1]
    r = dap.cmd([0x05, 0x00, 0x01, 0xA5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
    return hexs(r) if r else "NO RESP"


def swj_pins(dap):
    r = dap.cmd([0x10, 0x00, 0x00, 0x00, 0x00, 0x00])
    if r and r[0] == 0x10:
        val = r[1]
        tdo = (val >> 3) & 1
        return "0x%02X TDO=%d" % (val, tdo)
    return "NO RESP"


def main():
    dev = find_dev()
    if dev is None:
        print("PROBE NOT FOUND")
        return
    print("device:", dev.product_name)
    dap = Dap(dev)

    print("1) pins          :", swj_pins(dap))

    # connect SWD (firmware sends J2S inside swd_connect)
    r = dap.cmd([0x02, 0x01])
    print("2) connect SWD   :", hexs(r) if r else "NO RESP")
    time.sleep(0.2)
    print("3) DPIDR (SWD)   :", read_dpidr(dap))
    print("4) pins          :", swj_pins(dap))

    # send SWD->JTAG sequence
    seq = sw2j_bytes()
    r = dap.cmd([0x12, len(seq) * 8] + list(seq))  # COUNT field = bits, not bytes
    print("5) SW2J sequence :", hexs(r) if r else "NO RESP")
    time.sleep(0.05)
    print("6) DPIDR (SWD)   :", read_dpidr(dap), "<- 0x07 expected if switch reached DP")
    print("7) pins          :", swj_pins(dap))

    # TLR then JTAG IDCODE
    dap.cmd([0x02, 0x02])  # connect JTAG (loads jtag pin config)
    time.sleep(0.1)
    dap.cmd([0x14, 0x01, 0x40, 0xFF])  # TMS=1 x8 (TLR)
    r = dap.cmd([0x16, 0xFF])
    print("8) JTAG IDCODE   :", hexs(r) if r else "NO RESP")
    print("9) pins          :", swj_pins(dap))

    dap.cmd([0x03])
    dap.close()


if __name__ == "__main__":
    main()
