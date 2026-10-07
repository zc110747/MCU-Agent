"""Check whether the SCI9 console is still alive: RDR/RX state and PC."""
import time
from pyocd.core.helpers import ConnectHelper
import read_regs as R

# SCI9 at 0x40209xxx? find from header instead: just probe common bases.
s = ConnectHelper.session_with_chosen_probe(target_override="cortex_m")
with s:
    t = s.target
    t.memory_map = R.build_map()
    t.halt()
    pc = t.read_core_register("pc")
    lr = t.read_core_register("lr")
    sp = t.read_core_register("sp")
    xpsr = t.read_core_register("xpsr")
    print(f"HALTED PC=0x{pc:08X} LR=0x{lr:08X} SP=0x{sp:08X} xPSR=0x{xpsr:08X}")
    # Decode a plausible fault: check if PC sits in a known handler region.
    for name, addr in [
        ("R_MSTP MSTPCRB", 0x40203000 + 0x04),
        ("PFS P208 (TXD9)", 0x40400800 + 2*0x40 + 8*4),
        ("PFS P209 (RXD9)", 0x40400800 + 2*0x40 + 9*4),
    ]:
        print(f"{name:20} 0x{addr:08X} = 0x{t.read32(addr):08X}")
    t.resume()
