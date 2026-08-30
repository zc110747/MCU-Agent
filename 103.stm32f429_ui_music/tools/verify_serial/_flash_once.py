#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Flash the firmware via the running OpenOCD telnet server (4444)."""
import socket, sys, time

HOST, PORT = "127.0.0.1", 4444
ELF = sys.argv[1] if len(sys.argv) > 1 else "build/stm32f429_ui_music.elf"


def send(s, cmd, wait=3.0):
    s.sendall((cmd + "\n").encode())
    s.settimeout(wait)
    out = []
    try:
        while True:
            b = s.recv(4096)
            if not b:
                break
            out.append(b.decode(errors="replace"))
            if ">" in out[-1][-3:] or "invalid" in out[-1].lower():
                break
    except Exception:
        pass
    return "".join(out)


def main():
    s = socket.create_connection((HOST, PORT), timeout=10)
    time.sleep(0.3)
    s.settimeout(0.5)
    try:
        while True:
            b = s.recv(4096)
            if not b:
                break
    except Exception:
        pass
    print("[FLASH] reset halt")
    print(send(s, "reset halt", wait=4.0))
    print("[FLASH] program %s verify reset" % ELF)
    r = send(s, "program %s verify reset" % ELF, wait=30.0)
    print(r)
    ok = ("Verified OK" in r) or ("** Verified OK **" in r)
    print("[FLASH] verified_ok=%s" % ok)
    s.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
