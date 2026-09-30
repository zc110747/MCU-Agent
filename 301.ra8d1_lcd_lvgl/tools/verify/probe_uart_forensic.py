#!/usr/bin/env python3
"""UART RX forensic: dump P208/P209 PFS + SCI9 regs, then reset and re-smoke.

SCI9 (R_SCI0_Type layout): SMR@0x00, SCR@0x01(? as bytes) - use the 8-bit
view via halfword reads: SCR@0x08? Use byte reads through read8.
Offsets (R_SCI0_Type): SMR 0x00, BRR 0x01, SCR 0x02, TDR 0x03, SSR 0x04,
RDR 0x05, TDRHL 0x06, RDRHL 0x07 ...
"""
import time
from pyocd.core.helpers import ConnectHelper

PFS_TXD = 0x40400800 + 2 * 0x40 + 8 * 4    # P208
PFS_RXD = 0x40400800 + 2 * 0x40 + 9 * 4    # P209
SCI9    = 0x40358900

def bits_pfs(w):
    return (f"PODR={w & 1} PIDR={(w >> 1) & 1} PDR={(w >> 2) & 1} "
            f"PCR={(w >> 3) & 1} PMR={(w >> 16) & 1} PSEL={(w >> 24) & 0x1F}")

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    print("== before reset ==")
    w = t.read32(PFS_TXD); print("  PFS_P208(TXD)", hex(w), bits_pfs(w))
    w = t.read32(PFS_RXD); print("  PFS_P209(RXD)", hex(w), bits_pfs(w))
    for name, off in (("SMR", 0x00), ("SCR", 0x02), ("SSR", 0x04)):
        v = t.read8(SCI9 + off)
        print(f"  SCI9.{name} = {v:#04x}")
    t.reset()
    time.sleep(0.1)
    t.resume()
    print("== target reset + resumed, wait 2 s ==")
    time.sleep(2.0)

import serial
s = serial.Serial("COM9", 115200, timeout=0.2)
out = bytearray()
t0 = time.time()
while time.time() - t0 < 3.0:
    out += s.read(4096)
print("boot rx bytes:", len(out))
print(out.decode(errors="replace")[:300])

s.write(b"\r\n"); time.sleep(0.3); out += s.read(4096)
s.write(b"cam stat\r\n"); time.sleep(0.5); out += s.read(4096)
print("== after poke ==")
print(out.decode(errors="replace")[-500:])
s.close()
