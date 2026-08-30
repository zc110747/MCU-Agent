#!/usr/bin/env python3
"""Hands-off verification of the music player over a TX-ONLY serial link.

The board's debug header exposes USART3 TX only (PB11/RX is not wired to the
on-board USB-UART), so host->target characters never arrive and the normal
UART console cannot be driven.  Instead this script drives the SAME command
dispatch path through the debugger-injection mailbox (see serial_cmd.h/.c):

  * write the NUL-terminated command into g_dbg_line
  * set g_dbg_pending = 1
  * the serial_cmd_task picks it up on its next poll and dispatches it exactly
    as a UART byte would - every key + the stress loop run untouched.

Each command PRINT_LOGs its result to TX, which we capture on COM7.  This
exercises toggle/next/prev/stop/seek/volume/load + a 20-cycle stress loop and
a CFSR self-check, without any human in the loop and without a working RX
line.  OpenOCD (SWD) and the CH340 (COM) are independent physical links, so we
run OpenOCD as a subprocess while a thread drains COM7.

Usage:
    python auto_verify_ocd.py COM7 115200
Exit code 0 = all checks passed.
"""
import sys
import time
import serial
import subprocess
import threading

LINE_ADDR = 0x20006448   # g_dbg_line  (from arm-none-eabi-nm)
PEND_ADDR = 0x20006444   # g_dbg_pending

# (command string, sleep_seconds_after, expected_substring_in_TX)
SCRIPT = [
    ("?",      0.6, "[SER ] cmds:"),
    ("n",      0.6, "[CMD] next"),
    ("v",      0.6, "[CMD] prev"),
    ("p",      0.6, "[CMD] toggle"),
    ("d",      0.6, "[DIAG] CFSR=0x00000000"),
    ("p",      0.6, "[CMD] toggle"),   # pause again
    ("+",      0.6, "[CMD] vol+"),
    ("-",      0.6, "[CMD] vol-"),
    ("k30",    0.6, "[CMD] seek 30%"),
    ("s",      0.6, "[CMD] seek 50%"),
    ("t1",     0.8, "[CMD] track 1"),
    ("z20",    11.0, "[STR] stress done"),
    ("d",      0.6, "[DIAG] CFSR=0x00000000"),
]


def build_ocd_cmds():
    cmds = ["init"]
    for cmd, slp, _ in SCRIPT:
        b = cmd.encode("ascii")
        for i, ch in enumerate(b):
            cmds.append("mwb 0x%08X %d" % (LINE_ADDR + i, ch))
        cmds.append("mwb 0x%08X 0" % (LINE_ADDR + len(b)))   # NUL term
        cmds.append("mwb 0x%08X 1" % PEND_ADDR)              # trigger
        cmds.append("sleep %d" % int(slp * 1000))            # ms (integer only)
    cmds.append("shutdown")
    return cmds


def main():
    if len(sys.argv) >= 2:
        port = sys.argv[1]
    else:
        port = "COM7"
    baud = int(sys.argv[2]) if len(sys.argv) >= 3 else 115200

    buf = bytearray()
    stop = False

    def reader():
        try:
            s = serial.Serial(port, baud, timeout=0.2)
        except Exception as e:
            print("SERIAL OPEN FAILED: %s" % e)
            return
        s.reset_input_buffer()
        while not stop:
            d = s.read(4096)
            if d:
                buf.extend(d)
        s.close()

    th = threading.Thread(target=reader, daemon=True)
    th.start()
    time.sleep(0.5)

    argv = ["openocd", "-f", "openocd.cfg",
            "-c", "reset_config none", "-c", "adapter speed 1000"]
    for c in build_ocd_cmds():
        argv += ["-c", c]

    print("=== driving console via SWD mailbox (%d commands) ===" % len(SCRIPT))
    r = subprocess.run(argv, capture_output=True, text=True, timeout=200)
    if r.returncode != 0:
        for ln in (r.stdout + r.stderr).splitlines():
            if "rror" in ln:
                print("  OCD ERR:", ln.strip()[:120])
    time.sleep(1.0)
    stop = True
    th.join(timeout=3)

    text = buf.decode("latin-1", "replace")
    print("=== captured %d bytes of TX ===" % len(buf))

    passed = 0
    failed = 0
    for cmd, _, expect in SCRIPT:
        if expect in text:
            print("PASS  '%-4s' -> saw %r" % (cmd, expect))
            passed += 1
        else:
            print("FAIL  '%-4s' -> missing %r" % (cmd, expect))
            failed += 1

    # also confirm the stress loop actually ran (cycle lines present)
    if "[STR] cycle=" in text:
        print("PASS  stress cycles observed")
    else:
        print("FAIL  no stress cycle lines")

    print("=== %d passed / %d failed ===" % (passed, failed))
    if failed == 0 and "[STR] cycle=" in text:
        print("VERDICT: PASS")
        return 0
    print("VERDICT: FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main())
