#!/usr/bin/env python3
"""Live SCCB toggle check v2: spam `cam scan` for 8 s while rapidly sampling
P1103/P50E PFS (PIDR bit1 = live pin level) so the ~6 ms scan window cannot
be missed.
"""
import threading
import time

from pyocd.core.helpers import ConnectHelper

PFS_SCL = 0x40400800 + 11 * 0x40 + 3 * 4   # P1103
PFS_SDA = 0x40400800 + 5 * 0x40 + 14 * 4   # P50E

samples = {"scl": set(), "sda": set(), "n": 0}
stop = False

def poll():
    with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                                 frequency=4000000,
                                                 halt_on_connect=False) as session:
        t = session.target
        while not stop:
            try:
                samples["scl"].add(t.read32(PFS_SCL) & 0x2)
                samples["sda"].add(t.read32(PFS_SDA) & 0x2)
                samples["n"] += 1
            except Exception:
                pass

th = threading.Thread(target=poll)
th.start()
time.sleep(0.5)

import serial
s = serial.Serial("COM9", 115200, timeout=1)
t0 = time.time()
while time.time() - t0 < 8.0:
    s.write(b"cam scan\r\n")
    time.sleep(0.1)
out = s.read(65536).decode(errors="replace")
s.close()

stop = True
th.join()
acks = out.count("ACK addr7")
print(f"scan ACK lines in output: {acks}")
print(f"samples={samples['n']}  SCL levels={sorted(hex(x) for x in samples['scl'])}"
      f"  SDA levels={sorted(hex(x) for x in samples['sda'])}")
if len(samples["scl"]) > 1:
    print("=> SCL toggles physically on the pin (PIDR changes).")
else:
    print("=> SCL never toggled at the pin during 8 s of repeated scans.")
