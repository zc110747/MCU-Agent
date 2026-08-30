#!/usr/bin/env python3
"""Timed snapshot of the MP3 decode progress to tell RUNNING (slow decode)
from BLOCKED (deadlock on s_lock).

Resumes the (currently halted) target, reads decode telemetry twice 5s apart
while running, and prints both.  If g_dec_produced / g_dec_reads move, the
prime is CPU-bound in mp3dec_decode_frame (just slow).  If they are frozen,
player_task is blocked (most likely on s_lock) -> a deadlock.

Usage:  python tools/audio/probe_decode_progress.py
"""

import os
import sys
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from verify_sai_clock import resolve_symbols  # noqa: E402

OPENOCD_CFG = os.path.join(ROOT, "openocd.cfg")
MDW_RE = re.compile(r"^\s*0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})\s*$") \
    if False else __import__("re").compile(r"^\s*0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})\s*$")

NAMES = ["g_dec_produced", "g_dec_reads", "g_dec_fread", "g_dec_eof",
         "g_ply_prime_ok", "g_ply_prime_enter", "s_need_prime", "g_ply_refill"]


def read_once(addrs):
    cmds = ["init", "resume", "sleep 5000", "halt"]
    for a in addrs:
        cmds.append("mdw 0x%08X" % a)
    argv = ["openocd", "-f", OPENOCD_CFG,
            "-c", "reset_config none", "-c", "adapter speed 1000"]
    for c in cmds:
        argv += ["-c", c]
    argv += ["-c", "shutdown"]
    p = subprocess.run(argv, capture_output=True, text=True, timeout=120, cwd=ROOT)
    out = (p.stdout or "") + (p.stderr or "")
    vals = {}
    for line in out.splitlines():
        m = MDW_RE.match(line)
        if m:
            vals[int(m.group(1), 16)] = int(m.group(2), 16)
    return vals


def main():
    sym = resolve_symbols()
    addrs = [sym[n] for n in NAMES if n in sym]
    if len(addrs) < len(NAMES):
        print("WARN: missing symbols; have %s" % sorted(sym.keys()))
    print(">> snapshot 1 (running 5s)")
    s1 = read_once(addrs)
    print(">> snapshot 2 (running 5s)")
    s2 = read_once(addrs)

    print("=" * 60)
    print("%-18s %12s %12s" % ("symbol", "snap1", "snap2"))
    moving = False
    for n in NAMES:
        if n not in sym:
            continue
        a = sym[n]
        v1 = s1.get(a)
        v2 = s2.get(a)
        print("%-18s %12s %12s" % (n, v1, v2))
        if v1 is not None and v2 is not None and v1 != v2:
            moving = True
    print("=" * 60)
    if moving:
        print("VERDICT: decode telemetry is MOVING -> player_task is RUNNING")
        print("         (mp3dec_decode_frame is just slow on a malformed frame).")
    else:
        print("VERDICT: decode telemetry is FROZEN -> player_task is BLOCKED")
        print("         (most likely deadlocked on s_lock; find the lock holder).")


if __name__ == "__main__":
    main()
