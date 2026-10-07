"""Track SCI9 CCR0.RIE across a reset and after each shell command."""
import time
import serial
from pyocd.core.helpers import ConnectHelper
import read_regs as R

SCI9 = 0x40358900
CCR0 = SCI9 + 0x08
RDR  = SCI9 + 0x00
FRSR = SCI9 + 0x50


def read_state(t, label):
    ccr0 = t.read32(CCR0)
    rdr = t.read32(RDR)
    frsr = t.read32(FRSR)
    rie = (ccr0 >> 16) & 1
    re = ccr0 & 1
    te = (ccr0 >> 4) & 1
    print(f"{label:24} CCR0=0x{ccr0:08X} RE={re} TE={te} RIE={rie}  RDR=0x{rdr:02X} FRSR={frsr}")


s = ConnectHelper.session_with_chosen_probe(target_override="cortex_m")
with s:
    t = s.target
    t.memory_map = R.build_map()

    t.reset_and_halt()
    t.resume()
    time.sleep(1.5)
    t.halt()
    read_state(t, "after reset+1.5s")

    # resume and let it settle into the shell
    t.resume()
    time.sleep(2.0)
    t.halt()
    read_state(t, "shell idle")

    t.resume()
    time.sleep(0.2)
    # send a command while running
    sp = serial.Serial("COM9", 115200, timeout=0.2)
    sp.write(b"ospi bb\r\n")
    time.sleep(2.0)
    sp.close()
    t.halt()
    read_state(t, "after 'ospi bb'")

    t.resume()
