#!/usr/bin/env python3
"""Comprehensive one-shot read of the player/SAI/DMA state on a RUNNING target
(no reset, no resume) to see exactly where playback is stuck.  Reuses the
verify_sai_clock resume-free read pattern (init + mdw works on a running core).

Usage:  python tools/audio/probe_state_now.py
"""

import os
import sys
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from verify_sai_clock import resolve_symbols  # noqa: E402

OPENOCD_CFG = os.path.join(ROOT, "openocd.cfg")
MDW_RE = __import__("re").compile(r"^\s*0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})\s*$")

REG = {
    "DMA2_S3_CR": 0x40020410,
    "DMA2_S3_NDTR": 0x4002042C,
    "SAI1_A_CR1": 0x40015804,
    "SAI1_A_SR": 0x40015818,
}
SYMS = ["g_ply_prime_ok", "g_ply_refill", "g_ply_refill_eof",
        "g_ply_prime_enter", "g_sai_tx_dma_ret", "g_sai_dma_cbs",
        "s_need_prime", "g_dec_produced", "g_dec_reads", "g_dec_fread",
        "g_player_task_handle"]


def read_once(addrs):
    cmds = ["init"]
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


def b(v, bit):
    return (v >> bit) & 1


def main():
    sym = resolve_symbols()
    order = list(REG.values()) + [sym[n] for n in SYMS if n in sym]
    # reconstruct s_state from s_need_prime (np) + np+4
    np = sym.get("s_need_prime")
    if np is not None:
        order += [np, np + 4]
    v = read_once(order)

    def R(name):
        a = REG.get(name)
        return v.get(a) if a is not None else None

    def S(name):
        a = sym.get(name)
        return v.get(a) if (a is not None and a in v) else None

    cr1 = R("SAI1_A_CR1")
    cr = R("DMA2_S3_CR")
    ndtr = R("DMA2_S3_NDTR")
    sr = R("SAI1_A_SR")
    saien = (cr1 & 0x00010000) != 0 if cr1 is not None else None
    dmaen = (cr1 & 0x00020000) != 0 if cr1 is not None else None

    ply_state = None
    need = None
    if np is not None and v.get(np) is not None and v.get(np + 4) is not None:
        w0 = v[np]
        w1 = v[np + 4]
        need = w0 & 0xFF
        ply_state = (w0 >> 8) | (w1 << 24)

    print("=" * 60)
    print("PLAYER / SAI / DMA STATE (running target)")
    print("=" * 60)
    print("SAI1_A CR1 = 0x%08X  SAIEN(b16)=%s DMAEN(b17)=%s"
          % (cr1 if cr1 else 0, saien, dmaen))
    print("SAI1_A SR   = 0x%08X  FIFO level=%d"
          % (sr if sr else 0, ((sr >> 16) & 7) if sr is not None else -1))
    print("DMA2_S3_CR  = 0x%08X  EN=%d CIRC=%d TCIE=%d HTIE=%d"
          % (cr if cr else 0, b(cr, 0) if cr else -1, b(cr, 8) if cr else -1,
             b(cr, 4) if cr else -1, b(cr, 3) if cr else -1))
    print("DMA2_S3_NDTR= 0x%08X" % (ndtr if ndtr is not None else 0))
    names = {0: "STOPPED", 1: "PLAYING", 2: "PAUSED", 3: "PRIMING"}
    print("player state= %s (need_prime=%s)"
          % (names.get(ply_state, "?%s" % ply_state), need))
    print("g_ply_prime_enter = %s" % S("g_ply_prime_enter"))
    print("g_ply_prime_ok    = %s" % S("g_ply_prime_ok"))
    print("g_ply_refill      = %s" % S("g_ply_refill"))
    print("g_ply_refill_eof  = %s" % S("g_ply_refill_eof"))
    print("g_sai_tx_dma_ret  = %s (0xFFFFFFFF=never)" % S("g_sai_tx_dma_ret"))
    print("g_sai_dma_cbs     = %s" % S("g_sai_dma_cbs"))
    print("g_dec_produced    = %s" % S("g_dec_produced"))
    print("g_dec_reads       = %s" % S("g_dec_reads"))
    print("g_dec_fread       = %s" % S("g_dec_fread"))
    print("=" * 60)


if __name__ == "__main__":
    main()
