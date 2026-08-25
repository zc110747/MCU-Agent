#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QSPI test log capture helper.

The firmware mirrors every UART print into a RAM buffer (g_uart_log) so the
test output can be read reliably over SWD, avoiding the flaky ST-Link VCP
boot-log capture. This script:

  1. locates build/stm32_qspi.elf
  2. reads the g_uart_log / g_uart_log_len symbol addresses
  3. resets + runs the target via OpenOCD, waits, halts, dumps the buffer
  4. prints the decoded log to stdout

Usage:  python capture.py [--wait SECONDS] [--elf PATH]
Requires: arm-none-eabi-nm on PATH, openocd on PATH, ST-Link connected.
"""
import subprocess
import sys
import os
import argparse
import glob

WORKDIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ELF = os.path.join(WORKDIR, "build", "stm32_qspi.elf")
OPENOCD_CFG = os.path.join(WORKDIR, "openocd.cfg")
LOG_SIZE = 0x4000  # UART_LOG_BUF_SIZE


def find_symbol(elf, name):
    out = subprocess.check_output(["arm-none-eabi-nm", elf], text=True)
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in ("B", "b", "D", "d") and parts[2] == name:
            return int(parts[0], 16)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", default=DEFAULT_ELF)
    ap.add_argument("--wait", type=int, default=6)
    args = ap.parse_args()

    if not os.path.exists(args.elf):
        print(f"[ERR] ELF not found: {args.elf}", file=sys.stderr)
        sys.exit(1)

    addr = find_symbol(args.elf, "g_uart_log")
    if addr is None:
        print("[ERR] g_uart_log symbol not found in ELF", file=sys.stderr)
        sys.exit(1)
    print(f"[info] g_uart_log @ 0x{addr:08X}, dumping {LOG_SIZE} bytes after {args.wait}s run")

    # OpenOCD mangles absolute Windows paths (strips the drive colon / slashes),
    # so dump to a bare relative name and resolve it robustly afterwards.
    dump_name = "qspi_log.bin"
    cmds = [
        "openocd",
        "-f", OPENOCD_CFG,
        "-c", f"reset run",
        "-c", f"sleep {args.wait}",
        "-c", "halt",
        "-c", f"dump_image {dump_name} 0x{addr:08X} {LOG_SIZE}",
        "-c", "shutdown",
    ]
    subprocess.run(cmds, check=True)

    # The file may land as the bare name or with the cwd concatenated (colon/slash
    # stripped). Pick whichever exists; prefer the most recently modified.
    candidates = []
    for pat in (dump_name, f"*{dump_name}", f"*{dump_name.replace('.', '')}*"):
        candidates += glob.glob(os.path.join(WORKDIR, pat))
        candidates += glob.glob(pat)
    candidates = [c for c in candidates if os.path.isfile(c)]
    if not candidates:
        print(f"[ERR] dump file not found (looked for {dump_name})", file=sys.stderr)
        sys.exit(1)
    bin_path = max(candidates, key=os.path.getmtime)
    with open(bin_path, "rb") as f:
        data = f.read()
    text = data.split(b"\x00")[0].decode("utf-8", errors="replace")
    print("===== CAPTURED LOG =====")
    print(text)
    print("===== END LOG =====")


if __name__ == "__main__":
    main()
