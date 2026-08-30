#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Hardware play-freeze verification on COM7 (USART3 print console).

Captures the serial log, drives p/n/v, and reads CFSR via a single-shot
OpenOCD (halt + mdw, then shutdown -- never leaves a server running).
"""
import subprocess
import sys
import threading
import time

import serial

PORT = "COM7"
BAUD = 115200
OCD = "openocd"
CFG = "openocd.cfg"
CFSR_ADDR = "0xE000ED28"
HFSR_ADDR = "0xE000ED24"


def main():
    print("[MON] opening %s @%d" % (PORT, BAUD))
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.2)
    except Exception as e:
        print("[MON] serial open FAILED: %s" % e)
        return 2
    time.sleep(0.5)
    ser.reset_input_buffer()

    lines = []
    lock = threading.Lock()
    stop = threading.Event()

    def reader():
        while not stop.is_set():
            try:
                b = ser.read(ser.in_waiting or 1)
            except Exception:
                break
            if not b:
                continue
            for ln in b.decode(errors="replace").splitlines():
                if ln.strip():
                    with lock:
                        lines.append(ln)
                    print("[SERIAL] " + ln.rstrip())

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    def send(ch, delay):
        time.sleep(delay)
        ser.write(ch.encode())
        ser.flush()
        print("[SEND] %r" % ch)

    # Board already booted post-flash; give it a moment, then exercise commands.
    send("p", 5.0)   # play/toggle -> decode path (was the freeze)
    send("n", 6.0)   # next
    send("v", 6.0)   # prev
    time.sleep(6.0)  # let decode/refill run, capture any late output
    stop.set()
    t.join(timeout=2.0)
    try:
        ser.close()
    except Exception:
        pass

    text = "\n".join(lines)
    saw_toggle = "[CMD] toggle" in text
    saw_next = "[CMD] next" in text
    saw_prev = "[CMD] prev" in text
    saw_decode = "[DEC] MP3 open" in text
    n_tracks = 0
    for ln in lines:
        if "scanned" in ln and "track" in ln:
            try:
                n_tracks = int(ln.split("scanned")[1].split("track")[0].strip())
            except Exception:
                pass

    print("=" * 60)
    print("[MON] tracks scanned     : %d" % n_tracks)
    print("[MON] decode ran (MP3)   : %s" % saw_decode)
    print("[MON] play echoed         : %s" % saw_toggle)
    print("[MON] next echoed         : %s" % saw_next)
    print("[MON] prev echoed         : %s" % saw_prev)

    # Read fault registers (single-shot openocd, halt then shutdown).
    print("[MON] reading CFSR/HFSR via single-shot OpenOCD ...")
    try:
        out = subprocess.run(
            [OCD, "-f", CFG, "-c", "reset_config none", "-c",
             "adapter speed 1000", "-c", "init", "-c", "halt",
             "-c", "mdw " + CFSR_ADDR, "-c", "mdw " + HFSR_ADDR,
             "-c", "resume", "-c", "shutdown"],
            capture_output=True, text=True, timeout=60).stdout
    except Exception as e:
        out = ""
        print("[MON] openocd CFSR read failed: %s" % e)
    print("[MON] openocd CFSR output:\n" + out)

    cfsr_val = 0
    for ln in out.splitlines():
        if "0x" in ln and ("E000ED28" in ln or "mdw" in ln.lower()):
            pass
    # parse "0xE000ED28: 0xXXXXXXXX"
    import re
    m = re.search(r"0x%X:\s*0x([0-9A-Fa-f]{8})" % int(CFSR_ADDR, 16), out)
    if m:
        cfsr_val = int(m.group(1), 16)
    print("[MON] CFSR = 0x%08X" % cfsr_val)

    alive = saw_toggle and saw_next and saw_prev
    no_fault = (cfsr_val & 0xFFFFFF00) == 0
    if alive and no_fault:
        print("[MON] VERDICT: PASS - no HardFault, board alive after play")
        return 0
    if cfsr_val != 0:
        print("[MON] VERDICT: FAIL - CFSR nonzero (HardFault occurred)")
        return 3
    print("[MON] VERDICT: FAIL - board unresponsive after play")
    return 4


if __name__ == "__main__":
    sys.exit(main())
