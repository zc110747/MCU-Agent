#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Drive a direct call to BFLASH_ProgramBlock via openocd+gdb on a scratch flash
sector (0x08040000 = bank1 sector 2). If the call HANGS (the original bug),
this script kills the blocked gdb and attaches a second gdb to halt the target
and dump PC + backtrace -- i.e. "查看停止的地方".

Usage: python run_flash_test.py
Prereqs: openocd debug server running (gdb port 3333), ST-Link attached.
"""
import subprocess, sys, os, time

WORKDIR = os.path.dirname(os.path.abspath(__file__))
ROOT    = os.path.dirname(WORKDIR)
ELF     = os.path.join(ROOT, "build", "stm32h7_boot.elf")
GDB_IN  = os.path.join(WORKDIR, "flash_test_gdb.in")
GDB     = "D:/Software/tools/arm-none-eabi/bin/arm-none-eabi-gdb.exe"
TIMEOUT = 45  # seconds; a correct ProgramBlock returns in <1s

def run(cmd, timeout=None, capture=True):
    p = subprocess.run(cmd, capture_output=capture, text=True, timeout=timeout)
    return p.returncode, p.stdout, p.stderr

def main():
    if not os.path.exists(ELF):
        print("ERROR: elf not found:", ELF); sys.exit(1)

    print("[*] running gdb-driven BFLASH_ProgramBlock test (timeout %ds)..." % TIMEOUT)
    start = time.time()
    try:
        p = subprocess.run(
            [GDB, "-q", "-nx", "-batch", "-x", GDB_IN, ELF],
            capture_output=True, text=True, timeout=TIMEOUT)
        elapsed = time.time() - start
        print(p.stdout)
        if p.stderr.strip():
            print("--- gdb stderr ---\n" + p.stderr)
        print("[+] gdb returned cleanly in %.1fs (returncode %d)" % (elapsed, p.returncode))
        return
    except subprocess.TimeoutExpired as e:
        elapsed = time.time() - start
        print("[!] gdb did NOT return within %ds -> target likely HUNG in BFLASH_ProgramBlock" % TIMEOUT)
        # kill the blocked gdb (target keeps running, hung)
        if e.process:
            e.process.kill()
        print("[*] attaching a second gdb to HALT the target and read PC/backtrace ...")
        probe = (
            "set pagination off\n"
            "set confirm off\n"
            "target extended-remote :3333\n"
            "monitor halt\n"
            "info registers pc\n"
            "info registers r0 r1 r2 r3\n"
            "bt\n"
            "x/12i $pc\n"
            "quit\n"
        )
        try:
            pp = subprocess.run(
                [GDB, "-q", "-nx", "-batch", "-ex", probe, ELF],
                capture_output=True, text=True, timeout=30)
            print(pp.stdout)
            if pp.stderr.strip():
                print("--- probe stderr ---\n" + pp.stderr)
        except Exception as ex:
            print("[!] probe failed:", ex)
        sys.exit(2)

if __name__ == "__main__":
    main()
