#!/usr/bin/env python3
"""Discriminate: does 'cam init' kill the msh RX path?
reset -> cam stat (alive?) -> cam init -> cam stat / led blink (alive?)."""
import time
from pyocd.core.helpers import ConnectHelper

def chat(s, cmd, wait=1.0):
    s.reset_input_buffer()
    s.write(cmd.encode() + b"\r\n")
    t0 = time.time()
    buf = bytearray()
    while time.time() - t0 < wait:
        buf += s.read(4096)
    return buf.decode(errors="replace")

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    t.reset()
    time.sleep(0.1)
    t.resume()
print("reset done, waiting for boot..."); time.sleep(2.0)

import serial
s = serial.Serial("COM9", 115200, timeout=0.2)

print("--- poke 1: blank line ---")
print(repr(chat(s, "", 0.8))[:200])
print("--- cam stat (expect alive) ---")
print(repr(chat(s, "cam stat", 1.0))[:300])
print("--- cam init (expect scan + result) ---")
print(repr(chat(s, "cam init", 5.0))[:600])
print("--- cam stat #2 (alive after init?) ---")
print(repr(chat(s, "cam stat", 1.5))[:300])
print("--- led blink (alive?) ---")
print(repr(chat(s, "led blink", 1.0))[:200])
print("--- cam scan (explicit) ---")
print(repr(chat(s, "cam scan", 3.0))[:400])
s.close()
