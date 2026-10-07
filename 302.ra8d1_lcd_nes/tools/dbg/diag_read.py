#!/usr/bin/env python3
"""Read the crash-proof kernel snapshot (bsp_diag) over SWD.

The snapshot is written from the SysTick handler, so it stays valid even when
every task is wedged and the msh console is silent.

CRITICAL: the ELF passed in MUST be the same build that is flashed.  Symbol
addresses shift by a few bytes whenever the firmware is rebuilt, and a stale
address silently reads a *neighbouring* variable - which looks exactly like
memory corruption and will send you chasing a bug that does not exist.

Usage:
    python tools/dbg/diag_read.py [firmware.elf]
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "verify"))

import probe_ospi_regs as P  # noqa: E402

MAGIC = 0x52413844  # 'RA8D'

NM = "arm-none-eabi-nm"
DEFAULT_ELF = "build/firmware.elf"

MARK_NAMES = {
    0:  "boot",
    10: "console lock ready",
    11: "creating led thread",
    12: "lcd init",
    13: "gpu init",
    14: "ospi init",
    15: "nes heap",
    16: "entering main loop",
}

STAT = {0: "INIT", 1: "READY", 2: "RUN", 3: "SUSPEND", 4: "CLOSE"}


def sym(elf, name):
    out = subprocess.run([NM, elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == name:
            return int(parts[0], 16)
    raise SystemExit(f"symbol not found in {elf}: {name}")


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_ELF
    if not os.path.exists(elf):
        raise SystemExit(f"no such ELF: {elf}")

    base = sym(elf, "g_bsp_diag")
    # field order must match bsp_diag_t in bsp_diag.h
    fields = ["magic", "seq", "rt_tick_now", "ready_group", "cur_thread",
              "int_nest", "reserved_", "rx_pending", "last_code", "last_code_tick"]

    print(f"ELF            : {elf}")
    print(f"g_bsp_diag @   : {base:#010x}")

    sess = P.build_session()
    t = sess.target
    try:
        t.halt()
        vals = {}
        for i, name in enumerate(fields):
            vals[name] = t.read32(base + i * 4)
        t.resume()

        if vals["magic"] != MAGIC:
            print(f"magic          : {vals['magic']:#010x}  (expected {MAGIC:#010x})")
            print("=> snapshot was never written: SysTick never ran, or this ELF")
            print("   does not match the flashed firmware.")
            return

        print(f"seq            : {vals['seq']}")
        print(f"rt_tick        : {vals['rt_tick_now']}")
        print(f"ready_group    : {vals['ready_group']:#010x}" +
              ("   <-- NOTHING READY" if vals["ready_group"] == 0 else ""))
        cur = vals["cur_thread"]
        print(f"cur_thread     : {cur:#010x}")
        print(f"interrupt_nest : {vals['int_nest']}")
        print(f"rx_pending     : {vals['rx_pending']}")
        mark = vals["last_code"]
        print(f"last_mark      : {mark} ({MARK_NAMES.get(mark, '?')}) "
              f"@ tick {vals['last_code_tick']}")

        # Decode the current thread's name (rt_object.name is the first field).
        if cur:
            try:
                raw = t.read_memory_block8(cur, 12)
                name = bytes(raw).split(b"\x00")[0].decode("ascii", "replace")
                stat = t.read8(cur + 56)
                print(f"cur name       : {name!r} stat={stat} ({STAT.get(stat, '?')})")
            except Exception as exc:
                print(f"cur name       : <unreadable: {exc}>")
    finally:
        sess.close()


if __name__ == "__main__":
    main()
