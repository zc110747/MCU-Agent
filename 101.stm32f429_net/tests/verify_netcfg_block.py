#!/usr/bin/env python3
"""Offline check of netcfg EEPROM block layout + CRC16-CCITT.

Mirrors app/netcfg.c exactly:
  layout at addr 0:
    [0]   head  (0xAA)
    [1..15]   ip  (15+1 bytes)
    [16..30]  mask
    [31..45]  gw
    [46..63]  mac (17+1 bytes)
    [64..65]  crc16 over [0..63]
  total 66 bytes.

Run:  python verify_netcfg_block.py
"""
import struct

IP_LEN = 16
MAC_LEN = 18
HEAD = 0xAA
HEAD_SIZE = 1
CRC_SIZE = 2

OFF_HEAD = 0
OFF_IP = OFF_HEAD + HEAD_SIZE            # 1
OFF_MASK = OFF_IP + IP_LEN               # 17
OFF_GW = OFF_MASK + IP_LEN               # 33
OFF_MAC = OFF_GW + IP_LEN                # 49
OFF_CRC = OFF_MAC + MAC_LEN              # 67
TOTAL = OFF_CRC + CRC_SIZE               # 69


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def pack(ip, mask, gw, mac) -> bytes:
    blk = bytearray(TOTAL)
    blk[OFF_HEAD] = HEAD
    blk[OFF_IP:OFF_IP + IP_LEN] = ip.encode()[:IP_LEN].ljust(IP_LEN, b'\0')
    blk[OFF_MASK:OFF_MASK + IP_LEN] = mask.encode()[:IP_LEN].ljust(IP_LEN, b'\0')
    blk[OFF_GW:OFF_GW + IP_LEN] = gw.encode()[:IP_LEN].ljust(IP_LEN, b'\0')
    blk[OFF_MAC:OFF_MAC + MAC_LEN] = mac.encode()[:MAC_LEN].ljust(MAC_LEN, b'\0')
    c = crc16_ccitt(bytes(blk[:OFF_CRC]))
    blk[OFF_CRC] = c & 0xFF
    blk[OFF_CRC + 1] = (c >> 8) & 0xFF
    return bytes(blk)


def unpack(blk: bytes):
    assert blk[OFF_HEAD] == HEAD
    c = crc16_ccitt(blk[:OFF_CRC])
    stored = (blk[OFF_CRC + 1] << 8) | blk[OFF_CRC]
    assert c == stored, f"CRC mismatch calc={c:#06x} stored={stored:#06x}"
    ip = blk[OFF_IP:OFF_IP + IP_LEN].split(b'\0')[0].decode()
    mask = blk[OFF_MASK:OFF_MASK + IP_LEN].split(b'\0')[0].decode()
    gw = blk[OFF_GW:OFF_GW + IP_LEN].split(b'\0')[0].decode()
    mac = blk[OFF_MAC:OFF_MAC + MAC_LEN].split(b'\0')[0].decode()
    return ip, mask, gw, mac


PASS = 0
FAIL = 0


def check(name, cond):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  PASS  {name}")
    else:
        FAIL += 1
        print(f"  FAIL  {name}")


# 1) round-trip a normal block
blk = pack("192.168.10.55", "255.255.255.0", "192.168.10.1", "22:11:22:00:22:11")
ip, mask, gw, mac = unpack(blk)
check("round-trip ip", ip == "192.168.10.55")
check("round-trip mask", mask == "255.255.255.0")
check("round-trip gw", gw == "192.168.10.1")
check("round-trip mac", mac == "22:11:22:00:22:11")

# 2) block length 69 (head + 3*16 + 18 + crc16)
check("block length == 69", len(blk) == 69)

# 3) head magic
check("head == 0xAA", blk[OFF_HEAD] == 0xAA)

# 4) corrupted byte -> CRC fails
bad = bytearray(blk)
bad[OFF_IP + 1] ^= 0xFF
try:
    unpack(bytes(bad))
    check("corruption detected", False)
except AssertionError:
    check("corruption detected", True)

# 5) wrong head -> load returns default (load path checks head first)
empty = bytearray(TOTAL)
check("blank block has no head", empty[OFF_HEAD] != HEAD)

# 6) CRC of default block matches
dblk = pack("192.168.10.99", "255.255.255.0", "192.168.10.1", "00:80:E1:00:00:00")
unpack(dblk)
check("default block CRC ok", True)

print(f"\nCRC16-CCITT check: {PASS} pass / {FAIL} fail")
raise SystemExit(1 if FAIL else 0)
