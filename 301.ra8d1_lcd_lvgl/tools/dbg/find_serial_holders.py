#!/usr/bin/env python3
"""List processes holding a serial port open (best effort, Windows).

Enumerates python.exe processes and prints PID + cmdline so a stale serial
reader can be identified and killed manually.  Optionally kills any process
whose command line contains one of the given keywords (excluding self).
"""
import os
import sys

import psutil

KEYWORDS = sys.argv[1:] or [
    "console_diag",
    "reset_capture",
    "probe_serial",
    "flash.py",
]

me = os.getpid()
for p in psutil.process_iter(["pid", "name", "cmdline"]):
    try:
        if p.pid == me:
            continue
        cl = " ".join(p.info["cmdline"] or [])
        if not cl:
            continue
        if any(k in cl for k in KEYWORDS):
            print(f"PID={p.pid} NAME={p.info['name']} CMD={cl[:160]}")
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        continue
