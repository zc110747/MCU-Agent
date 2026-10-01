#!/usr/bin/env python3
"""List available serial ports (Windows)."""
from serial.tools import list_ports

for p in list_ports.comports():
    print(f"{p.device}\t{p.description}\t{p.hwid}")
