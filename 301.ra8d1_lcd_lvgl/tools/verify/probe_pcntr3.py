#!/usr/bin/env python3
"""Decisive PCNTR3-vs-PFS experiment for P1103 (SCCB SCL) / P50E (SDA).

Phase A (halted): debugger drives PCNTR3 (correct address 0x40400168 =
R_PORT11_BASE + PCNTR3 offset 0x08, per R7FA8D1BH.h) and checks whether
P1103 PODR/PIDR follow. Isolates the write path from the firmware.
  - PODR toggles + PIDR follows  -> PCNTR3 path physically works.
  - PODR toggles + PIDR stuck    -> pin is INPUT (PDR=0): init state lost
    or never applied; PCNTR3 is innocent.

Phase B (running): "cam init" then "cam scan" spam while sampling
P1103/P50E PIDR (bit 1 of the PFS word) at 4 MHz, capturing serial
output as proof the scans actually executed.
"""
import threading
import time

from pyocd.core.helpers import ConnectHelper

PFS_SCL   = 0x40400800 + 11 * 0x40 + 3 * 4    # P1103 PFS word
PFS_SDA   = 0x40400800 + 5 * 0x40 + 14 * 4   # P50E  PFS word
PCNTR1_11 = 0x40400160                        # R_PORT11_BASE + 0x00
PCNTR3_11 = 0x40400160 + 0x08                 # POSR[15:0] / PORR[31:16]

def bits(w):
    return (f"PODR={w & 1} PIDR={(w >> 1) & 1} PDR={(w >> 2) & 1} "
            f"PMR={(w >> 16) & 1} PSEL={(w >> 24) & 0x1F}")

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target

    print("== Phase A: pin state before touching anything ==")
    try:
        w = t.read32(PFS_SCL);   print("  PFS_SCL  ", hex(w), bits(w))
        w = t.read32(PFS_SDA);   print("  PFS_SDA  ", hex(w), bits(w))
        w = t.read32(PCNTR1_11); print("  PCNTR1_11", hex(w),
                                       f"PDR11={((w >> 0) >> 3) & 1}",
                                       f"PODR11={((w >> 16) >> 3) & 1}")
    except Exception as e:
        print("  read failed:", e)

    print("== Phase A: halt, drive P1103 via PCNTR3 (set/clear/set) ==")
    t.halt()
    try:
        t.write32(PCNTR3_11, 0x00000008)      # POSR: latch P1103 high
        w = t.read32(PFS_SCL); print("  after SET ", hex(w), bits(w))
        t.write32(PCNTR3_11, 0x00080000)      # PORR: latch P1103 low
        w = t.read32(PFS_SCL); print("  after CLR ", hex(w), bits(w))
        t.write32(PCNTR3_11, 0x00000008)      # restore idle high
        w = t.read32(PFS_SCL); print("  after SET2", hex(w), bits(w))
    except Exception as e:
        print("  PCNTR3 write failed:", e)
    t.resume()

    print("== Phase B: cam init + scan spam, sampling both pins ==")
    samples = {"scl": set(), "sda": set(), "n": 0, "err": 0}
    stop = False

    def poll():
        while not stop:
            try:
                samples["scl"].add(t.read32(PFS_SCL) & 0x2)
                samples["sda"].add(t.read32(PFS_SDA) & 0x2)
                samples["n"] += 1
            except Exception:
                samples["err"] += 1

    th = threading.Thread(target=poll)
    th.start()

    import serial
    out = bytearray()
    s = serial.Serial("COM9", 115200, timeout=0.05)
    t0 = time.time()
    s.write(b"cam init\r\n")
    t1 = time.time()
    while time.time() - t1 < 3.0:
        out += s.read(4096)
    while time.time() - t0 < 12.0:
        s.write(b"cam scan\r\n")
        for _ in range(10):
            out += s.read(4096)
            time.sleep(0.01)
    s.close()

    stop = True
    th.join()
    text = out.decode(errors="replace")
    print("  serial: cam init replies:", text.count("cam init:"),
          "| scan replies:", text.count("cam scan:"),
          "| no-ACK lines:", text.count("no device ACKed"),
          "| ACK lines:", text.count("ACK addr7"))
    print(f"  samples={samples['n']} errors={samples['err']}  "
          f"SCL={sorted(hex(x) for x in samples['scl'])}  "
          f"SDA={sorted(hex(x) for x in samples['sda'])}")

    print("== Post-run pin state ==")
    try:
        w = t.read32(PFS_SCL); print("  PFS_SCL  ", hex(w), bits(w))
        w = t.read32(PFS_SDA); print("  PFS_SDA  ", hex(w), bits(w))
    except Exception as e:
        print("  read failed:", e)
