#!/usr/bin/env python3
"""Live bitbang verification with a HEALTHY shell: connect -> reset (known
pitfall: pyOCD connect deafens the shell until next reset) -> cam init/scan
spam while sampling P1103/P50E PIDR (bit 1 of PFS word)."""
import threading
import time

from pyocd.core.helpers import ConnectHelper

PFS_SCL = 0x40400800 + 11 * 0x40 + 3 * 4    # P1103
PFS_SDA = 0x40400800 + 5 * 0x40 + 14 * 4   # P50E

samples = {"scl": set(), "sda": set(), "n": 0}
stop = False

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    t.reset(); time.sleep(0.1); t.resume()
    print("target reset for healthy shell, waiting boot...")
    time.sleep(2.0)

    def poll():
        while not stop:
            try:
                samples["scl"].add(t.read32(PFS_SCL) & 0x2)
                samples["sda"].add(t.read32(PFS_SDA) & 0x2)
                samples["n"] += 1
            except Exception:
                pass

    th = threading.Thread(target=poll)
    th.start()

    import serial
    out = bytearray()
    s = serial.Serial("COM9", 115200, timeout=0.05)
    s.write(b"cam init\r\n")
    t1 = time.time()
    while time.time() - t1 < 5.0:
        out.extend(s.read(4096))
    t0 = time.time()
    while time.time() - t0 < 8.0:
        s.write(b"cam scan\r\n")
        for _ in range(10):
            out.extend(s.read(4096))
            time.sleep(0.01)
    s.close()
    stop = True
    th.join()

    text = out.decode(errors="replace")
    print("init replies:", text.count("[cam] scan"),
          "| scan replies:", text.count("cam scan:"))
    for line in text.splitlines():
        if "[cam]" in line or "cam scan:" in line:
            print("  |", line.strip())
    print(f"samples={samples['n']}  "
          f"SCL={sorted(hex(x) for x in samples['scl'])}  "
          f"SDA={sorted(hex(x) for x in samples['sda'])}")
