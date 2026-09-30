#!/usr/bin/env python3
"""CMSIS-DAP over WiFi (TCP) diagnostic client.

Mirrors tools/run_jtag_transfer.py but talks TCP instead of HID, and uses the
length-prefixed frame the firmware expects (see components/wifi_dap/wifi_dap.h):

    [ cmsis_dap_tcp_packet_hdr_t ][ CMSIS-DAP payload ]

The payload is the raw DAP command (NO HID report-id 0x00 prefix) — the same
bytes cmsis_dap_execute() receives on the USB path after TinyUSB strips the
report id.

This client does NOT need OpenOCD. It validates the wireless transport end to
end: the TCP frame is delimited correctly, cmsis_dap_execute() runs, and the
response comes back framed. Connect/SWD/JTAG commands that touch the target
will only succeed if a target board is wired up; the DAP_Info exchange alone
proves the WiFi link works.

Usage:
    python wifi_dap_client.py                 # default 192.168.4.1:50000
    python wifi_dap_client.py 192.168.4.1     # custom host
    python wifi_dap_client.py 192.168.4.1 50000
"""
import socket
import struct
import sys
import time

HDR_SIG = 0x00504144          # "DAP\0" little-endian
PKT_REQUEST = 0x01
PKT_RESPONSE = 0x02
HDR_SIZE = 8                  # sizeof(cmsis_dap_tcp_packet_hdr_t)
DEFAULT_HOST = "192.168.4.1"
DEFAULT_PORT = 50000


def make_frame(payload, ptype=PKT_REQUEST):
    hdr = struct.pack("<IHBB", HDR_SIG, len(payload), ptype, 0x00)
    return hdr + bytes(payload)


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("connection closed while reading %d bytes" % n)
        buf += chunk
    return buf


def recv_frame(sock):
    hdr = recv_exact(sock, HDR_SIZE)
    sig, length, ptype, _ = struct.unpack("<IHBB", hdr)
    if sig != HDR_SIG:
        raise ValueError("bad header signature 0x%08X" % sig)
    if ptype != PKT_RESPONSE:
        raise ValueError("unexpected packet_type 0x%02X" % ptype)
    payload = recv_exact(sock, length) if length else b""
    return payload


class Dap:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5.0)
        self.sock.settimeout(5.0)

    def cmd(self, p):
        self.sock.sendall(make_frame(p))
        return recv_frame(self.sock)

    def close(self):
        self.sock.close()


def hexs(b):
    return " ".join("%02X" % x for x in (b or []))


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_HOST
    port = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_PORT

    print("connecting %s:%d" % (host, port))
    try:
        d = Dap(host, port)
    except Exception as e:
        print("CONNECT FAILED: %s" % e)
        return 2
    print("connected")

    # 1) DAP_Info(0x00 VendorID) — proves the wireless link without a target.
    r = d.cmd([0x00, 0x00])
    print("DAP_Info Vendor :", hexs(r), "->", r[2:].decode("latin1").rstrip("\x00") if len(r) > 2 else "")

    r = d.cmd([0x00, 0x01])  # ProductID
    print("DAP_Info Product:", hexs(r), "->", r[2:].decode("latin1").rstrip("\x00") if len(r) > 2 else "")

    r = d.cmd([0x00, 0xFC])  # Capabilities (v1)
    print("DAP_Info Caps   :", hexs(r))

    # 2) Connect SWD
    r = d.cmd([0x02, 0x01])
    print("Connect SWD     :", hexs(r), "status=%s" % (r[1] if len(r) > 1 else "?"))

    # 3) Connect JTAG (mirrors run_jtag_transfer.py sequence, sans HID prefix)
    r = d.cmd([0x02, 0x02])
    print("Connect JTAG    :", hexs(r), "status=%s" % (r[1] if len(r) > 1 else "?"))

    r = d.cmd([0x15, 0x02, 0x04, 0x05])   # JTAG_Configure: 2 TAPs cpu=4 bs=5
    print("JTAG_Configure  :", hexs(r))

    for i in range(2):
        r = d.cmd([0x16, i])              # JTAG_IDCODE per TAP
        print("JTAG_IDCODE %d  :" % i, hexs(r))

    r = d.cmd([0x03])                     # Disconnect
    print("Disconnect      :", hexs(r))

    d.close()
    print("DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
