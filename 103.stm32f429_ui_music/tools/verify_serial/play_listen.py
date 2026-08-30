#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Listen on COM7 for 90s while the user clicks PLAY/NEXT/PREV on the LCD.
Captures [DEC]/[CMD] logs and reads CFSR at the end (single-shot openocd)."""
import re
import subprocess
import sys
import threading
import time

import serial

PORT = "COM7"
BAUD = 115200
LISTEN_S = 150


def main():
    print("[LIS] opening %s @%d" % (PORT, BAUD))
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.2)
    except Exception as e:
        print("[LIS] serial open FAILED: %s" % e)
        return 2
    time.sleep(0.3)
    ser.reset_input_buffer()

    buf = bytearray()
    lines = []
    lock = threading.Lock()

    def reader():
        while True:
            try:
                b = ser.read(ser.in_waiting or 1)
            except Exception:
                break
            if b:
                buf.extend(b)
                txt = b.decode(errors="replace")
                sys.stdout.write("[RX] " + txt)
                sys.stdout.flush()
                for ln in txt.splitlines():
                    if ln.strip():
                        with lock:
                            lines.append(ln)

    threading.Thread(target=reader, daemon=True).start()

    # Fresh, known state.
    print("[LIS] resetting board for clean state ...", flush=True)
    subprocess.run(["openocd", "-f", "openocd.cfg", "-c", "reset_config none",
                    "-c", "adapter speed 1000", "-c", "init", "-c", "reset run",
                    "-c", "shutdown"], capture_output=True, text=True, timeout=60)

    print("[LIS] === LISTEN %ds -- click PLAY/NEXT/PREV on the LCD now ===" % LISTEN_S,
          flush=True)
    time.sleep(LISTEN_S)

    text = "\n".join(lines)
    saw_dec = "[DEC] MP3 open" in text
    saw_cmd = "[CMD]" in text
    saw_err = ("HardFault" in text) or ("BUSFAULT" in text) or ("MEMFAULT" in text)
    last_rx = time.time()

    print("[LIS] === reading CFSR/HFSR (halt+resume) ===", flush=True)
    try:
        out = subprocess.run(
            ["openocd", "-f", "openocd.cfg", "-c", "reset_config none",
             "-c", "adapter speed 1000", "-c", "init", "-c", "halt",
             "-c", "mdw 0xE000ED28", "-c", "mdw 0xE000ED24", "-c", "resume",
             "-c", "shutdown"],
            capture_output=True, text=True, timeout=60).stdout
    except Exception as e:
        out = ""
        print("[LIS] CFSR read failed: %s" % e)
    print("[LIS] openocd:\n" + out)

    cfsr = 0
    m = re.search(r"0xe000ed28:\s*0x([0-9A-Fa-f]{8})", out)
    if m:
        cfsr = int(m.group(1), 16)

    print("=" * 60)
    print("[LIS] total bytes : %d" % len(buf))
    print("[LIS] [DEC] MP3 open seen : %s" % saw_dec)
    print("[LIS] [CMD] echoed       : %s" % saw_cmd)
    print("[LIS] fault strings      : %s" % saw_err)
    print("[LIS] CFSR               : 0x%08X" % cfsr)
    no_fault = (cfsr & 0xFFFFFF00) == 0
    if saw_dec and no_fault:
        print("[LIS] VERDICT: PASS - decode started, no HardFault after LCD clicks")
        rc = 0
    elif cfsr != 0:
        print("[LIS] VERDICT: FAIL - CFSR nonzero (HardFault)")
        rc = 3
    else:
        print("[LIS] VERDICT: INCONCLUSIVE - no [DEC] MP3 open captured (did play start?)")
        rc = 4
    try:
        ser.close()
    except Exception:
        pass
    return rc


if __name__ == "__main__":
    sys.exit(main())
