#!/usr/bin/env python3
"""Reset target then run cam init so GPT7/CEU are in the post-init state."""
import time
import serial
from pyocd.core.helpers import ConnectHelper

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    t.reset(); time.sleep(0.1); t.resume()
time.sleep(2.0)

s = serial.Serial("COM9", 115200, timeout=0.2)
s.write(b"cam init\r\n")
t0 = time.time()
out = bytearray()
while time.time() - t0 < 5.0:
    out.extend(s.read(4096))
print(out.decode(errors="replace").strip()[-200:])
s.close()
