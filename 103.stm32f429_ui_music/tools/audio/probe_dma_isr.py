#!/usr/bin/env python3
"""Focused probe: WHY does the DMA2_Stream3 ISR never fire?

Symptoms (from prior SWD reads):
  * DMA2_LISR = 0x00400000  -> TCIF3 (Transfer-Complete, bit 22) SET, stuck
  * g_sai_dma_cbs = 0       -> the TxHalfCplt/TxCplt callbacks never ran
  * NDTR = 0                -> frozen; player never refills, SAI stops

HAL_DMA_IRQHandler ALWAYS clears the TCIF/HTIF flags, so a stuck SET flag
means the ISR body is simply not executing.  This probe reads the whole DMA
-> NVIC -> vector-table chain to localise the break:

  1. DMA2_Stream3 CR : EN(b0) CIRC(b8) HTIE(b3) TCIE(b4) TEIE(b2) PL(b16..b17)
       - if TCIE/HTIE == 0 the DMA hardware never requests the interrupt
  2. DMA2_LISR       : TCIF3(b22) HTIF3(b20) TEIF3(b25) FEIF3(b16)
       - TEIF3 set would mean a DMA transfer ERROR (not just "no refill")
  3. NVIC ISER1      : bit 31 (DMA2_Stream3_IRQn=63 -> ISER1 bit 31)
       - if 0 the interrupt is NOT enabled in the NVIC
  4. NVIC ISPR1      : bit 31 -> if set, the ISR is pending but masked/blocked
  5. NVIC ICSR       : VECTPENDING / VECTACTIVE (who is running)
  6. VTOR            : vector table base
  7. vector[79]      : VTOR + (16+63)*4 = VTOR + 0x13C  -> must be
                       DMA2_Stream3_IRQHandler|+1 (0x0800984d).  If it points
                       to Default_Handler (0x0800abbd) the weak binding lost.

Usage:  python tools/audio/probe_dma_isr.py [--sleep MS]
"""

import os
import re
import sys
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from verify_sai_clock import resolve_symbols, reset_run, inject_line  # noqa: E402

OPENOCD_CFG = os.path.join(ROOT, "openocd.cfg")
MDW_RE = re.compile(r"^\s*0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})\s*$")

# DMA2_Stream3 registers
DMA2_S3_CR = 0x40020410
DMA2_LISR = 0x40026400
# NVIC
NVIC_ISER1 = 0xE000E104
NVIC_ISPR1 = 0xE000E204
NVIC_ICSR = 0xE000ED04
VTOR = 0xE000ED08
# DMA2_Stream3_IRQn = 59 (confirmed in stm32f429xx.h) -> vector entry 16+59=75
# -> offset 75*4 = 0x12C.  NVIC ISER1 bit = 59-32 = 27.
VEC_OFF = (16 + 59) * 4
VEC_BIT = 59 - 32  # bit in ISER1/ISPR1

HANDLER_ADDR = 0x0800984C   # from nm (T DMA2_Stream3_IRQHandler)
DEFAULT_HANDLER = 0x0800ABBC


def read_words_once(addrs):
    cmds = ["init"]
    for a in addrs:
        cmds.append("mdw 0x%08X" % a)
    cmds.append("shutdown")
    argv = ["openocd", "-f", OPENOCD_CFG,
            "-c", "reset_config none", "-c", "adapter speed 1000"]
    for c in cmds:
        argv += ["-c", c]
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
    sleep_ms = 8000
    for a in sys.argv[1:]:
        if a.startswith("--sleep"):
            sleep_ms = int(a.split("=", 1)[1]) if "=" in a else 8000

    sym = resolve_symbols()
    dbg_line = sym.get("g_dbg_line")
    dbg_pend = sym.get("g_dbg_pending")
    if not dbg_line or not dbg_pend:
        print("ERROR: mailbox symbols not resolved")
        sys.exit(2)

    print(">> reset + run (clean state)")
    reset_run()
    print(">> inject 't1' (load+play track 1)")
    inject_line("t1", dbg_line, dbg_pend)

    # Pass 1: everything except the vector (need VTOR first)
    print(">> let it run %d ms, then read DMA/NVIC/VTOR" % sleep_ms)
    addrs1 = [DMA2_S3_CR, DMA2_LISR, NVIC_ISER1, NVIC_ISPR1,
              NVIC_ICSR, VTOR]
    # also the player telemetry of interest
    for name in ("g_sai_dma_cbs", "g_ply_refill", "g_ply_prime_ok",
                 "s_need_prime"):
        if name in sym:
            addrs1.append(sym[name])
    v1 = read_words_once(addrs1)

    vtor = v1.get(VTOR)
    vec_addr = (vtor + VEC_OFF) if vtor is not None else (0x08000000 + VEC_OFF)
    print(">> read vector[79] at 0x%08X" % vec_addr)
    v2 = read_words_once([vec_addr])
    vec = v2.get(vec_addr)

    def g(name):
        return v1.get(sym[name]) if (name in sym and sym[name] in v1) else None

    cr = v1.get(DMA2_S3_CR)
    lisr = v1.get(DMA2_LISR)
    iser1 = v1.get(NVIC_ISER1)
    ispr1 = v1.get(NVIC_ISPR1)
    icsr = v1.get(NVIC_ICSR)

    print("=" * 68)
    print("DMA2_Stream3 ISR PROBE")
    print("=" * 68)
    if cr is not None:
        print("DMA2_S3_CR = 0x%08X" % cr)
        print("   EN(b0)=%d  CIRC(b8)=%d  HTIE(b3)=%d  TCIE(b4)=%d  TEIE(b2)=%d  PL(b16..17)=%d"
              % (b(cr, 0), b(cr, 8), b(cr, 3), b(cr, 4), b(cr, 2),
                 (cr >> 16) & 0x03))
    if lisr is not None:
        print("DMA2_LISR  = 0x%08X" % lisr)
        print("   FEIF3(b16)=%d  HTIF3(b20)=%d  TCIF3(b22)=%d  TEIF3(b25)=%d"
              % (b(lisr, 16), b(lisr, 20), b(lisr, 22), b(lisr, 25)))
    if iser1 is not None:
        print("NVIC ISER1 = 0x%08X   DMA2_Stream3 enable(b%d)=%d"
              % (iser1, VEC_BIT, b(iser1, VEC_BIT)))
    if ispr1 is not None:
        print("NVIC ISPR1 = 0x%08X   DMA2_Stream3 pending(b%d)=%d"
              % (ispr1, VEC_BIT, b(ispr1, VEC_BIT)))
    if icsr is not None:
        vectactive = icsr & 0x1FF
        vectpending = (icsr >> 12) & 0x1FF
        print("NVIC ICSR  = 0x%08X   VECTACTIVE=%d  VECTPENDING=%d"
              % (icsr, vectactive, vectpending))
    if vtor is not None:
        print("VTOR       = 0x%08X   vector[79]@0x%08X = 0x%08X"
              % (vtor, vec_addr, vec if vec is not None else 0))
        if vec is not None:
            target = vec & ~1
            if target == HANDLER_ADDR:
                print("   -> points to DMA2_Stream3_IRQHandler (CORRECT)")
            elif target == DEFAULT_HANDLER:
                print("   -> points to Default_Handler (WEAK BINDING LOST!)")
            else:
                print("   -> points to 0x%08X (UNEXPECTED)" % target)
    print("-" * 68)
    print("g_sai_dma_cbs = %s" % (hex(g("g_sai_dma_cbs"))
          if g("g_sai_dma_cbs") is not None else "n/a"))
    print("g_ply_refill  = %s" % g("g_ply_refill"))
    print("g_ply_prime_ok= %s" % g("g_ply_prime_ok"))
    print("s_need_prime  = %s" % g("s_need_prime"))
    print("=" * 68)

    # Diagnosis
    print()
    print("DIAGNOSIS:")
    problems = []
    if cr is not None:
        if b(cr, 4) == 0 and b(cr, 3) == 0:
            problems.append("DMA TCIE/HTIE not enabled -> DMA never requests IRQ")
        if b(cr, 0) == 0:
            problems.append("DMA stream EN=0 -> no transfer at all")
    if iser1 is not None and b(iser1, VEC_BIT) == 0:
        problems.append("NVIC ISER1 bit%d=0 -> DMA2_Stream3 IRQ not enabled" % VEC_BIT)
    if (lisr is not None and b(lisr, 25) == 1):
        problems.append("TEIF3 set -> DMA transfer ERROR")
    if vec is not None and (vec & ~1) == DEFAULT_HANDLER:
        problems.append("vector table points to Default_Handler")
    if not problems:
        problems.append("All DMA/NVIC/vector wiring looks correct -> "
                        "ISR should fire; investigate silent fault / BASEPRI")
    for p in problems:
        print("  - " + p)


if __name__ == "__main__":
    main()
