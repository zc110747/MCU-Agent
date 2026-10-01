#!/usr/bin/env python3
"""Poll the CST812T INT line (P010) over SWD and report edges.

CTP_IRQ_N is on P010.  A CST812T with no finger present keeps INT released
(high, via the pull-up / its open-drain output off).  Any physical touch
pulses INT low.  Sampling PIDR (PCNTR2 bit 10) therefore gives a hardware
ground-truth "did the chip see a touch" signal, completely independent of the
I2C driver.

Usage:  python p010_watch.py [seconds]
While it runs, touch the panel.  Lines/edges are printed as they occur.
"""
import sys
import time

from pyocd.core.session import Session
from pyocd.probe.aggregator import DebugProbeAggregator

# PCNTR2 is at R_PFS.PORT[0] + 0x04 (PORR/PIDR).  P010 is bit 10.
P0_PCNTR2 = 0x40400800 + 0x04
P010_MASK = 1 << 10


def get_probe(retries=25, delay=0.5):
    for _ in range(retries):
        ps = DebugProbeAggregator.get_all_connected_probes()
        if ps:
            return ps[0]
        time.sleep(delay)
    return None


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    probe = get_probe()
    if probe is None:
        sys.exit("no probe after retries")
    print("probe:", probe.unique_id)
    session = Session(probe, target_override="cortex_m",
                      frequency=1000000, options={"no_config": True})
    session.open()
    t = session.target

    print(f"Polling P010 (CTP_IRQ_N) for {secs:.0f}s - touch the panel now...")
    end = time.time() + secs
    last = None
    lows = 0
    n = 0
    while time.time() < end:
        t.halt()
        v = t.read32(P0_PCNTR2)
        t.resume()
        lvl = 1 if (v & P010_MASK) else 0
        n += 1
        if lvl == 0:
            lows += 1
        if lvl != last:
            print(f"  t={time.time():.3f}  PIDR.P010={'HIGH' if lvl else 'LOW '} "
                  f"(pcntr2=0x{v:08X})")
            last = lvl
        time.sleep(0.02)
    print(f"\nSampled {n} times, P010 low in {lows} samples.")
    if lows == 0:
        print("  -> INT never asserted: chip either not touched, not powered, or dead.")
    else:
        print("  -> INT pulsed: the CST812T is alive and detecting touches!")
    session.close()


if __name__ == "__main__":
    main()
