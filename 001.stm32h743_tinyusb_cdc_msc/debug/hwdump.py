#!/usr/bin/env python3
"""Live DCMI / DMA2_Stream1 register dump over SWD (target keeps running).

    python debug/hwdump.py            # one snapshot
    python debug/hwdump.py --watch 2  # snapshot, wait 2 s, snapshot again

Peripheral registers are readable through the AHB-AP while the core runs, so
this never halts the CPU and never perturbs the capture pipeline.
"""

import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPENOCD = os.environ.get("OPENOCD", r"D:/Software/openocd/bin/openocd.exe")
SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", r"D:/Software/openocd/share/openocd/scripts")
CFG = os.path.join(ROOT, "debug", "openocd.cfg").replace("\\", "/")

DCMI = 0x48020000
DMA2 = 0x40020400
S1 = DMA2 + 0x10 + 0x18 * 1          # DMA2_Stream1
MUX_C9 = 0x40020800 + 4 * 9          # DMAMUX1 channel for DMA2_Stream1

REGS = [
    ("DCMI_CR",   DCMI + 0x00),
    ("DCMI_SR",   DCMI + 0x04),
    ("DCMI_RISR", DCMI + 0x08),
    ("DCMI_IER",  DCMI + 0x0C),
    ("DCMI_MISR", DCMI + 0x10),
    ("DMA2_LISR", DMA2 + 0x00),
    ("S1_CR",     S1 + 0x00),
    ("S1_NDTR",   S1 + 0x04),
    ("S1_PAR",    S1 + 0x08),
    ("S1_M0AR",   S1 + 0x0C),
    ("S1_M1AR",   S1 + 0x10),
    ("S1_FCR",    S1 + 0x14),
    ("DMAMUX_C9", MUX_C9),
]

SYMS = [
    "cam_frame_count", "cam_error_count", "cam_start_count", "cam_start_fail_count",
    "cam_ovr_count", "cam_sync_err_count", "cam_restart_count", "cam_watchdog_count",
    "g_loop_count", "g_fault_id", "g_cam_status",
    "usb_mount_count", "uvc_frames_sent", "uvc_frames_dropped",
    "uvc_xfer_started", "uvc_xfer_rejected", "uvc_state",
]


def bits(val, table):
    """table: list of (name, lsb, width). Returns 'A=1 B=3' for non-zero fields."""
    out = []
    for name, lsb, width in table:
        f = (val >> lsb) & ((1 << width) - 1)
        if f:
            out.append(f"{name}={f}")
    return " ".join(out)


DCMI_CR_BITS = [("CAPTURE", 0, 1), ("CM_snapshot", 1, 1), ("CROP", 2, 1),
                ("JPEG", 3, 1), ("ESS", 4, 1), ("PCKPOL", 5, 1), ("HSPOL", 6, 1),
                ("VSPOL", 7, 1), ("FCRC", 8, 2), ("EDM", 10, 2), ("ENABLE", 14, 1)]
DCMI_IRQ_BITS = [("FRAME", 0, 1), ("OVR", 1, 1), ("ERR", 2, 1),
                 ("VSYNC", 3, 1), ("LINE", 4, 1)]
DCMI_SR_BITS = [("HSYNC", 0, 1), ("VSYNC", 1, 1), ("FNE", 2, 1)]
S1CR_BITS = [("EN", 0, 1), ("DMEIE", 1, 1), ("TEIE", 2, 1), ("HTIE", 3, 1),
             ("TCIE", 4, 1), ("PFCTRL", 5, 1), ("DIR", 6, 2), ("CIRC", 8, 1),
             ("PINC", 9, 1), ("MINC", 10, 1), ("PSIZE", 11, 2), ("MSIZE", 13, 2),
             ("PL", 16, 2), ("DBM", 18, 1), ("CT", 19, 1),
             ("PBURST", 21, 2), ("MBURST", 23, 2)]
# DMA2_LISR, stream 1 occupies bits 6..11
LISR_BITS = [("FEIF1", 6, 1), ("DMEIF1", 8, 1), ("TEIF1", 9, 1),
             ("HTIF1", 10, 1), ("TCIF1", 11, 1)]

DECODE = {
    "DCMI_CR": DCMI_CR_BITS, "DCMI_SR": DCMI_SR_BITS,
    "DCMI_RISR": DCMI_IRQ_BITS, "DCMI_IER": DCMI_IRQ_BITS,
    "DCMI_MISR": DCMI_IRQ_BITS, "S1_CR": S1CR_BITS, "DMA2_LISR": LISR_BITS,
}


def resolve_symbols():
    elf = os.path.join(ROOT, "build", "stm32h743_uvc.elf")
    if not os.path.exists(elf):
        return {}
    try:
        out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True,
                             text=True, check=True).stdout
    except Exception:
        return {}
    addrs = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] in SYMS:
            addrs[p[2]] = int(p[0], 16)
    return addrs


def read_words(targets):
    """targets: list of (label, addr). Returns {label: value}."""
    cmds = [OPENOCD, "-s", SCRIPTS, "-f", CFG, "-c", "init"]
    for _, addr in targets:
        cmds += ["-c", f"mdw 0x{addr:08x} 1"]
    cmds += ["-c", "shutdown"]
    p = subprocess.run(cmds, capture_output=True, text=True)
    blob = p.stdout + p.stderr
    # OpenOCD prints "0x40020428: 00000000"
    found = {}
    for m in re.finditer(r"0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})", blob):
        found[int(m.group(1), 16)] = int(m.group(2), 16)
    if not found:
        sys.stderr.write(blob[-1500:] + "\n")
        raise SystemExit("no register reads came back - is the probe connected?")
    return {label: found.get(addr) for label, addr in targets}


def snapshot(symaddr):
    targets = list(REGS) + [(s, a) for s, a in symaddr.items()]
    return read_words(targets)


def show(vals, symaddr, prev=None):
    print("=" * 68)
    print("  peripheral registers")
    print("=" * 68)
    for name, _ in REGS:
        v = vals.get(name)
        if v is None:
            continue
        dec = bits(v, DECODE[name]) if name in DECODE else ""
        if name == "S1_NDTR":
            dec = f"{v} words left"
        print(f"  {name:<11} 0x{v:08x}  {dec}")

    if symaddr:
        print("=" * 68)
        print("  firmware counters")
        print("=" * 68)
        for s in SYMS:
            if s not in symaddr:
                continue
            v = vals.get(s)
            if v is None:
                continue
            delta = ""
            if prev and prev.get(s) is not None and v != prev[s]:
                delta = f"   (+{v - prev[s]})"
            print(f"  {s:<22} {v:<12} 0x{v:08x}{delta}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--watch", type=float, default=0,
                    help="seconds between two snapshots (shows deltas)")
    args = ap.parse_args()

    symaddr = resolve_symbols()
    first = snapshot(symaddr)
    show(first, symaddr)

    if args.watch > 0:
        time.sleep(args.watch)
        second = snapshot(symaddr)
        print(f"\n\n### after {args.watch:g} s ###")
        show(second, symaddr, prev=first)


if __name__ == "__main__":
    main()
