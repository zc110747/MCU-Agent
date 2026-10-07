"""Read the MISO/P803 pin level while it is left as a plain input.

If the flash is present and idle, MISO floats and would read as whatever the
board's pull-up gives (normally high).  A stuck-low reading points at a short
or an absent device; a toggling reading would prove signalling.
"""
import time
from pyocd.core.helpers import ConnectHelper
import read_regs as R

PFS_P803 = 0x40400800 + 8 * 0x40 + 3 * 4   # port 8 pin 3 -> offset 0x20C

s = ConnectHelper.session_with_chosen_probe(target_override="cortex_m")
with s:
    t = s.target
    t.memory_map = R.build_map()
    t.halt()

    raw = t.read32(PFS_P803)
    pidr = (raw >> 1) & 1
    pdr = (raw >> 2) & 1
    pcr = (raw >> 4) & 1
    psel = (raw >> 24) & 0x1F
    pmr = (raw >> 16) & 1
    print(f"P803 PmnPFS = 0x{raw:08X}")
    print(f"  PSEL=0x{psel:02X} PMR={pmr} PDR(dir)={pdr} PIDR(level)={pidr} PCR(pullup)={pcr}")

    # Sample the level several times to see whether it is static or toggling.
    levels = []
    for _ in range(20):
        levels.append((t.read32(PFS_P803) >> 1) & 1)
        time.sleep(0.02)
    print(f"  PIDR samples: {levels}")
    print(f"  stable={len(set(levels))==1} level={levels[0]}")

    t.resume()
