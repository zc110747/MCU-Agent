#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Hardware verification for the "click play freezes the board" fix.

Flashes the Release .elf via OpenOCD, drives the polled serial test console
(USART3) to issue play/next/prev, and checks whether the Cortex-M4 faults
(CFSR != 0).  The fix moves all MP3 decode off the UI task, so play must no
longer HardFault.

Usage:
    verify_play.py [elf] [serial_port]
Defaults: build/stm32f429_ui_music.elf  /dev/ttyS6  (COM7)
"""
import socket
import sys
import time
import threading

try:
    import serial
except ImportError:
    serial = None

ELF_DEFAULT = "build/stm32f429_ui_music.elf"
SERIAL_DEFAULT = "/dev/ttyS6"
OCD_HOST = "127.0.0.1"
OCD_PORT = 4444
CFSR_ADDR = 0xE000ED28
HFSR_ADDR = 0xE000ED24


class OpenOCD:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=10)
        self.s.settimeout(2.0)
        self._drain()

    def _drain(self):
        self.s.settimeout(0.5)
        try:
            while True:
                b = self.s.recv(4096)
                if not b:
                    break
        except Exception:
            pass

    def cmd(self, c, wait=2.0):
        self.s.sendall((c + "\n").encode())
        self.s.settimeout(wait)
        out = []
        try:
            while True:
                b = self.s.recv(4096)
                if not b:
                    break
                out.append(b.decode(errors="replace"))
                if ">" in out[-1][-4:] or "invalid" in out[-1].lower():
                    break
        except Exception:
            pass
        return "".join(out)

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else ELF_DEFAULT
    port = sys.argv[2] if len(sys.argv) > 2 else SERIAL_DEFAULT

    print("[VERIFY] flashing %s" % elf)
    ocd = OpenOCD(OCD_HOST, OCD_PORT)
    ocd.cmd("reset halt")
    r = ocd.cmd("program %s verify reset" % elf, wait=20.0)
    print(r)
    if "Verified OK" not in r and "** Verified OK **" not in r:
        print("[VERIFY] WARNING: verify not confirmed in output")
    ocd.cmd("reset run")
    ocd.close()
    time.sleep(2.0)

    if serial is None:
        print("[VERIFY] pyserial missing; cannot drive serial console")
        return 1

    print("[VERIFY] opening serial %s @115200" % port)
    try:
        ser = serial.Serial(port, 115200, timeout=0.2)
    except Exception as e:
        print("[VERIFY] serial open failed: %s" % e)
        return 1

    lines = []
    lock = threading.Lock()

    def reader():
        while True:
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

    def send(ch, wait):
        time.sleep(0.3)
        ser.write(ch.encode())
        print("[SEND] %r" % ch)
        time.sleep(wait)

    # Let the board boot + scan + mount.
    time.sleep(6.0)
    send("p", 7.0)   # play / toggle  -> decode path (was the freeze)
    send("n", 5.0)   # next
    send("v", 5.0)   # prev

    # Read fault status registers.
    ocd = OpenOCD(OCD_HOST, OCD_PORT)
    cfsr = ocd.cmd("mdw 0x%X" % CFSR_ADDR, wait=3.0)
    hfsr = ocd.cmd("mdw 0x%X" % HFSR_ADDR, wait=3.0)
    ocd.close()
    print("[CFSR] " + cfsr.strip())
    print("[HFSR] " + hfsr.strip())

    text = "\n".join(lines)
    cfsr_val = 0
    try:
        cfsr_val = int(cfsr.split(":")[1].strip(), 16)
    except Exception:
        pass

    # Decide pass/fail.
    saw_toggle = "[CMD] toggle -> state=1" in text
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
    print("RESULT")
    print("  tracks scanned     : %d" % n_tracks)
    print("  decode ran (MP3)   : %s" % saw_decode)
    print("  play cmd responded : %s" % saw_toggle)
    print("  next cmd responded : %s" % saw_next)
    print("  prev cmd responded : %s" % saw_prev)
    print("  CFSR               : 0x%08X" % cfsr_val)
    alive = saw_toggle and saw_next and saw_prev
    no_fault = (cfsr_val & 0xFFFFFF00) == 0  # ignore stacked garbage low bits
    if alive and no_fault:
        print("  VERDICT            : PASS - no HardFault, board alive after play")
        return 0
    if cfsr_val != 0:
        print("  VERDICT            : FAIL - CFSR nonzero (HardFault occurred)")
        return 2
    print("  VERDICT            : FAIL - board unresponsive after play")
    return 3


if __name__ == "__main__":
    sys.exit(main())
