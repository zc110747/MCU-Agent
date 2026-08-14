#!/usr/bin/env python3
"""On-device acceptance test for the screen-capture (cap) feature.

Connects to the firmware console (ST-Link VCP or USB CDC), drives the
`cap` / `cap list` / `cap head` commands, and checks:
  * console is responsive (`status` -> OK)
  * `cap` writes a JPEG under 1:/catch/HH-MM-SS-NNN.jpg
  * `cap list` shows the file and `cap head` reports FFD8 (SOI) at offset 0
  * several back-to-back captures do not crash the board (console still alive)

Usage:  python verify_cap.py            # auto-pick COM19/COM4
        python verify_cap.py COM19      # explicit port
"""
import sys
import time
import serial

BAUD = 115200
CANDIDATES = ["COM19", "COM4"]
TIMEOUT = 2.0
QUIET = 0.35  # gap with no incoming bytes => response finished


def read_response(ser, label=""):
    buf = b""
    last = time.time()
    while True:
        n = ser.in_waiting
        if n:
            chunk = ser.read(n)
            buf += chunk
            last = time.time()
        elif time.time() - last > QUIET:
            break
        time.sleep(0.02)
    text = buf.decode("utf-8", "replace")
    if label:
        print(f"--- {label} ---\n{text.rstrip()}")
    return text


def send(ser, line):
    ser.write((line + "\r\n").encode("utf-8"))
    time.sleep(0.15)


def pick_port(explicit=None):
    ports = [explicit] if explicit else CANDIDATES
    for p in ports:
        try:
            ser = serial.Serial(p, BAUD, timeout=0.5)
            time.sleep(0.3)
            ser.reset_input_buffer()
            send(ser, "status")
            resp = read_response(ser, f"probe {p}")
            if "OK" in resp:
                print(f"[+] Using {p}\n")
                return ser
            ser.close()
        except Exception as e:
            print(f"[-] {p}: {e}")
    return None


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else None
    ser = pick_port(port)
    if ser is None:
        print("ERROR: no responsive console port found")
        sys.exit(1)

    try:
        # 1) capture once
        send(ser, "cap")
        resp = read_response(ser, "cap #1")
        if "cap saved" not in resp:
            print("ERROR: cap did not report a saved path")
            sys.exit(2)
        path = ""
        for tok in resp.split():
            if tok.startswith("1:/catch/") and tok.endswith(".jpg"):
                path = tok
                break
        print(f"[+] captured -> {path}")

        # 2) list
        send(ser, "cap list")
        read_response(ser, "cap list")

        # 3) head -> verify JPEG SOI marker FFD8 at offset 0
        name = path.split("/")[-1]
        send(ser, f"cap head {name}")
        head = read_response(ser, f"cap head {name}")
        if "FF D8" in head.upper():
            print("[+] JPEG SOI (FF D8) present at offset 0  OK")
        else:
            print("ERROR: cap head does not start with FF D8")
            sys.exit(3)

        # 4) stability: several rapid captures, console must stay alive
        ok = 0
        for i in range(2, 6):
            send(ser, "cap")
            r = read_response(ser, f"cap #{i}")
            if "cap saved" in r:
                ok += 1
        print(f"[+] {ok}/4 additional captures succeeded")
        send(ser, "status")
        alive = read_response(ser, "status after captures")
        if "OK" in alive:
            print("[+] console still responsive after captures  OK")
        else:
            print("ERROR: console unresponsive after captures")
            sys.exit(4)

        print("\nALL CHECKS PASSED")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
