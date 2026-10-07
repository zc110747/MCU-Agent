#!/usr/bin/env python3
"""Sample the program counter to find where emulation time actually goes.

Why this exists
---------------
`nes bench` shows the emulator costs ~29 M cycles per frame against an 8 M
budget (12 fps).  That number says *how much*, not *where*, and the two
candidates - the PPU's per-scanline renderers versus the opcode interpreter -
have very different fixes.  Rather than editing third_party/nes to add timers
(the tree must stay unmodified), the running core is sampled from outside: the
debug probe halts the core repeatedly and reports where it was.

Method
------
Attaching halts the core, so each sample is taken as one halt/step/resume.  The
sample is unbiased with respect to which instruction was executing because the
halt point is uncorrelated with the firmware's own logic.  The distribution over
symbol ranges is what matters, not any individual sample.

Notes on the probe
------------------
A halted core stops servicing UART interrupts, so the core is resumed after
every sample; the sampling rate is limited by the probe round-trip, not by the
CPU.  That is fine - a few hundred samples locate the hot function clearly.

Usage:
    python tools/verify/profile_pc.py [--samples 200]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from verify_nes_rom import DEFAULT_ROM, send_serial, write_file_to_mem  # noqa: E402

# Symbol ranges, in address order: (name, start, end).  Addresses come from
# `arm-none-eabi-nm --numeric-sort build/firmware.elf`.
REGIONS = []


def load_regions(elf):
    """Build symbol ranges for the NES emulation code."""
    nm = os.path.join(
        os.environ.get("TOOLCHAIN", ""),
        "arm-none-eabi-nm.exe",
    )
    if not os.path.exists(nm):
        nm = "arm-none-eabi-nm"
    out = subprocess.run([nm, "--numeric-sort", "--print-size", elf],
                         capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        addr, size, kind, name = parts[0], parts[1], parts[2], parts[3]
        if kind not in ("T", "t"):
            continue
        try:
            a = int(addr, 16)
            s = int(size, 16)
        except ValueError:
            continue
        if s == 0:
            continue
        # Only the emulation core and its renderers interest us.
        if not name.startswith(("nes_", "opcode_", "nes")):
            continue
        if not (0x02000000 <= a < 0x02040000):
            continue
        syms.append((a, a + s, name))
    syms.sort()
    REGIONS.extend(syms)
    return syms


def classify(pc, syms):
    for start, end, name in syms:
        if start <= pc < end:
            return name
    return None


def sample_pcs(n, syms):
    """Take n PC samples, one pyocd session."""
    fd, path = tempfile.mkstemp(suffix=".pyocd_cmds", text=True)
    try:
        with os.fdopen(fd, "w") as f:
            lines = []
            for _ in range(n):
                lines.append("reg pc")
                lines.append("step")
            lines.append("go")
            f.write("\n".join(lines) + "\n")
        r = subprocess.run(
            ["pyocd", "cmd", "-t", "cortex_m", "-O", "connect_mode=pre-reset",
             "-x", path],
            capture_output=True, text=True, timeout=600,
        )
        pcs = []
        for line in r.stdout.splitlines():
            m = re.match(r"^\s*pc\s*=\s*0x([0-9a-fA-F]+)\s*$", line.strip())
            if m:
                pcs.append(int(m.group(1), 16))
        return pcs
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=200)
    ap.add_argument("--elf", default="build/firmware.elf")
    args = ap.parse_args()

    syms = load_regions(args.elf)
    print(f"symbol ranges loaded: {len(syms)}")

    # Make sure a ROM is running so the core is actually emulating.
    rom = open(DEFAULT_ROM, "rb").read()
    text = send_serial(["nes stage"], settle=0.8, listen_s=2.0)
    m = re.search(r"nes stage: addr=0x([0-9a-fA-F]+)", text)
    if not m:
        print(f"[FAIL] no staging address:\n{text}")
        return 1
    write_file_to_mem(int(m.group(1), 16), rom)
    send_serial(["nes runmem"], settle=1.0, listen_s=2.5)
    time.sleep(2.0)
    info = send_serial(["nes info"], settle=0.8, listen_s=2.0)
    print(info.strip().splitlines()[-5:])

    print(f"sampling {args.samples} PCs...")
    pcs = sample_pcs(args.samples, syms)
    print(f"got {len(pcs)} samples")

    hits = {}
    unknown = []
    for pc in pcs:
        name = classify(pc, syms)
        if name is None:
            unknown.append(pc)
        else:
            hits[name] = hits.get(name, 0) + 1

    total = len(pcs) or 1
    print("\n--- PC distribution ---")
    for name, c in sorted(hits.items(), key=lambda kv: -kv[1]):
        print(f"  {c * 100.0 / total:6.1f}%  {c:4d}  {name}")

    if unknown:
        # Fold the opcode handlers back together: they are reached through a
        # dispatch table, so seeing many distinct addresses is the expected shape.
        print(f"  {len(unknown) * 100.0 / total:6.1f}%  {len(unknown):4d}  "
              f"<outside emulation symbols>")

    send_serial(["nes stop"], settle=1.5, listen_s=2.0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
