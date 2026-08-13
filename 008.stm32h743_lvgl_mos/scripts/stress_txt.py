#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TXT reader stress test for the H743-NES firmware.

Goals
-----
1. Exercise the paginated TXT reader hard: seed a multi-page file, then open it,
   page to the end, page back, and close it -- repeatedly -- plus rapid
   open/close bursts, out-of-range indices and garbage commands.
2. Detect a hard *deadlock* (board stops answering the serial console) and, on
   deadlock, dump the fault state with OpenOCD + arm-none-eabi-gdb so the root
   cause can be pinned to a function/line.

Usage
-----
    stress_txt.py COM6 [--cycles 60] [--seed-name SEED.TXT] [--timeout 5]

Exit code 0 = PASS (no deadlock within the run), 1 = DEADLOCK detected.
"""
import sys
import os
import re
import time
import subprocess
import serial
import argparse

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(PROJ, "build-release", "nes_h743.elf")
OPENOCD_CFG = os.path.join(PROJ, "openocd.cfg")

OK_RE = re.compile(r"^(OK|ERR)\b")
PAGE_RE = re.compile(r"第\s*(\d+)\s*页")
STATE_RE = re.compile(r"^state\s*:\s*(\w+)")


def open_port(port, timeout):
    ser = serial.Serial(port, 115200, timeout=timeout,
                        bytesize=serial.EIGHTBITS,
                        parity=serial.PARITY_NONE,
                        stopbits=serial.STOPBITS_ONE)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


def send(ser, cmd):
    ser.write((cmd + "\r\n").encode("utf-8"))
    ser.flush()


def read_until_sentinel(ser, timeout):
    """Read lines until an OK/ERR line appears or `timeout` seconds elapse.
    Returns (ok_bool, all_text)."""
    end = time.time() + timeout
    buf = []
    while time.time() < end:
        try:
            line = ser.readline()
        except serial.SerialTimeoutException:
            line = b""
        if not line:
            continue
        txt = line.decode("utf-8", "replace").rstrip("\r\n")
        buf.append(txt)
        if OK_RE.match(txt):
            return True, "\n".join(buf)
    return False, "\n".join(buf)


def do_cmd(ser, cmd, timeout):
    send(ser, cmd)
    ok, text = read_until_sentinel(ser, timeout)
    return ok, text


def parse_state(text):
    for ln in text.splitlines():
        m = STATE_RE.match(ln.strip())
        if m:
            return m.group(1)
    return None


def parse_page(text):
    for ln in text.splitlines():
        m = PAGE_RE.search(ln)
        if m:
            return int(m.group(1))
    return None


def capture_crash(report_path):
    """Halt the target with OpenOCD and dump a backtrace + fault registers
    using arm-none-eabi-gdb against the gdb server.  Returns the captured text."""
    print("[CAP ] launching OpenOCD (halt + gdbserver) ...")
    ocd = subprocess.Popen(
        ["openocd", "-f", OPENOCD_CFG, "-c", "init", "-c", "halt"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    # Give OpenOCD time to come up and halt.
    time.sleep(4.0)

    gdb = None
    for cand in ("arm-none-eabi-gdb", "arm-none-eabi-gdb.exe"):
        gdb = cand
        break

    try:
        out = subprocess.run(
            [gdb, "-batch", "-ex", "set pagination off",
             "-ex", "file " + ELF,
             "-ex", "target remote :3333",
             "-ex", "bt",
             "-ex", "info reg pc lr sp msp psp",
             "-ex", "echo \n=== CFSR/HFSR/BFAR/MMFAR ===\n",
             "-ex", "x/wx 0xE000ED28",
             "-ex", "x/wx 0xE000ED2C",
             "-ex", "x/wx 0xE000ED38",
             "-ex", "x/wx 0xE000ED34",
             "-ex", "echo \n=== STACKED EXC FRAME @ MSP (R0,R1,R2,R3,R12,LR,PC,xPSR) ===\n",
             "-ex", "x/8xw $msp",
             "-ex", "echo \n=== FAULTING PC (frame[6] = MSP+24) ===\n",
             "-ex", "info symbol *(uint32_t*)($msp+24)",
             "-ex", "info line *(uint32_t*)($msp+24)",
             "-ex", "quit"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=30)
        captured = out.stdout
    except Exception as e:
        captured = "gdb capture failed: %s" % e
    finally:
        try:
            ocd.terminate()
        except Exception:
            pass
        try:
            ocd.wait(timeout=5)
        except Exception:
            ocd.kill()

    with open(report_path, "w", encoding="utf-8") as f:
        f.write(captured)
    return captured


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--cycles", type=int, default=60)
    ap.add_argument("--seed-name", default="SEED.TXT")
    ap.add_argument("--timeout", type=float, default=5.0)
    ap.add_argument("--no-seed", action="store_true")
    ap.add_argument("--dwell", type=int, default=75,
                    help="seconds to idle on the menu across a minute boundary")
    args = ap.parse_args()

    ser = open_port(args.port, args.timeout)
    print("[RUN ] port=%s cycles=%d timeout=%.1fs elf=%s" %
          (args.port, args.cycles, args.timeout, ELF))

    # 0. Seed a deterministic multi-page file so paging is exercised.
    if not args.no_seed:
        ok, txt = do_cmd(ser, "txt seed " + args.seed_name, args.timeout)
        print("[SEED] %s -> %s" % ("OK" if ok else "TIMEOUT",
                                   txt.replace("\n", " | ")))
        if not ok:
            print("[FAIL] seed did not respond; board already dead?")
            ser.close()
            return 1

    # Give the menu a tick to register the new file.
    time.sleep(0.3)
    ok, txt = do_cmd(ser, "txt list", args.timeout)
    print("[LIST] %s" % (txt.replace("\n", " | ") if ok else "TIMEOUT"))

    fail_streak = 0
    ops = 0
    deadlock = False

    def check(ok, label):
        nonlocal fail_streak, ops, deadlock
        ops += 1
        if ok:
            fail_streak = 0
        else:
            fail_streak += 1
            print("[!!  ] no response to '%s' (streak=%d)" % (label, fail_streak))
            if fail_streak >= 3:
                deadlock = True
                return False
        return True

    # 1. Main cycle: open -> page to end -> page back -> close.
    for c in range(args.cycles):
        if deadlock:
            break
        # open
        ok, txt = do_cmd(ser, "txt open 0", args.timeout)
        if not check(ok, "txt open 0"):
            break
        # wait until reader state=reading (tick completes the load)
        reading = False
        for _ in range(10):
            ok, txt = do_cmd(ser, "txt info", args.timeout)
            if not ok:
                if not check(False, "txt info"):
                    break
                else:
                    continue
            if parse_state(txt) == "reading":
                reading = True
                break
            time.sleep(0.05)
        if not reading:
            print("[WARN] reader did not enter 'reading' at cycle %d" % c)

        # page to the end
        last = parse_page(txt) or 1
        for _ in range(60):
            ok, t2 = do_cmd(ser, "key down", args.timeout)
            if not ok:
                if not check(False, "key down"):
                    break
                else:
                    continue
            pg = parse_page(t2)
            if pg is None or pg <= last:
                # reached the last page (no further advance) -> stop
                break
            last = pg
        # page back a bit
        for _ in range(5):
            ok, _ = do_cmd(ser, "key up", args.timeout)
            if not check(ok, "key up"):
                break
        # close back to the browser
        ok, _ = do_cmd(ser, "txt close", args.timeout)
        if not check(ok, "txt close"):
            break
        # liveness ping
        ok, _ = do_cmd(ser, "txt list", args.timeout)
        if not check(ok, "txt list"):
            break

        if (c + 1) % 10 == 0:
            print("[PROG] cycle %d/%d done, ops=%d" % (c + 1, args.cycles, ops))

    # 2. Rapid open/close burst (view rebuild churn).
    if not deadlock:
        for i in range(40):
            ok, _ = do_cmd(ser, "txt open 0", args.timeout)
            if not check(ok, "burst txt open"):
                break
            ok, _ = do_cmd(ser, "txt close", args.timeout)
            if not check(ok, "burst txt close"):
                break
        print("[BURST] 40 open/close bursts done, ops=%d" % ops)

    # 3. Error-path fuzz: out-of-range + garbage.
    if not deadlock:
        for cmd in ("txt open 99", "txt open -1", "txt open abc",
                    "txt frobnicate", "zzz", ""):
            if cmd == "":
                continue
            ok, _ = do_cmd(ser, cmd, args.timeout)
            check(ok, cmd)  # expecting ERR, not silence
        print("[FUZZ] error-path commands done, ops=%d" % ops)

    # 4. Minute-boundary dwell (the exact trigger of the 2026-08-13 freeze):
    #    open a page, return to the MAIN MENU (which frees the page header and
    #    its "HH:MM" clock label), then stay idle while the wall clock rolls
    #    over a minute.  Pre-fix, app_menu_tick() dereferenced the freed label
    #    here -> HardFault.  We ping every 2 s and require a reply throughout.
    if not deadlock:
        dwell = max(args.dwell, 1)
        print("[DWELL] open -> back to menu, then idle %ds across a minute "
              "boundary (pre-fix freeze point)" % dwell)
        do_cmd(ser, "txt open 0", args.timeout)
        do_cmd(ser, "txt close", args.timeout)   # -> browser
        do_cmd(ser, "key select", args.timeout)  # -> main menu (frees header)
        t0 = time.time()
        while (time.time() - t0) < dwell:
            ok, _ = do_cmd(ser, "status", args.timeout)
            if not check(ok, "dwell status ping"):
                break
            time.sleep(2.0)
        print("[DWELL] survived %ds idle on the menu, ops=%d" %
              (int(time.time() - t0), ops))

    ser.close()

    if deadlock:
        print("\n[DEADLOCK] board stopped responding after %d ops." % ops)
        print("[DEADLOCK] capturing fault state via OpenOCD + gdb ...")
        rep = os.path.join(PROJ, "scripts", "crash_txt.txt")
        cap = capture_crash(rep)
        print("---- crash capture (saved to %s) ----" % rep)
        print(cap)
        print("------------------------------------------")
        return 1

    print("\n[PASS] %d ops across %d cycles, no deadlock. TXT reader stable on %s."
          % (ops, args.cycles, args.port))
    return 0


if __name__ == "__main__":
    sys.exit(main())
