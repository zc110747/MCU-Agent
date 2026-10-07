"""Read the SCI9 console registers to see why the shell stops accepting input."""
from pyocd.core.helpers import ConnectHelper
import read_regs as R

BASE = 0x40358900

s = ConnectHelper.session_with_chosen_probe(target_override="cortex_m")
with s:
    t = s.target
    t.memory_map = R.build_map()
    t.halt()

    regs = [
        ("RDR  ", 0x00), ("TDR  ", 0x04), ("CCR0 ", 0x08), ("CCR1 ", 0x0C),
        ("CCR2 ", 0x10), ("CCR3 ", 0x14), ("CCR4 ", 0x18), ("CESR ", 0x1C),
        ("ICR  ", 0x20), ("FCR  ", 0x24), ("MCR  ", 0x2C), ("DCR  ", 0x30),
        ("CSR  ", 0x48), ("FRSR ", 0x50), ("FTSR ", 0x54),
    ]
    for name, off in regs:
        print(f"{name} 0x{BASE+off:08X} = 0x{t.read32(BASE+off):08X}")

    pc = t.read_core_register("pc")
    print(f"PC = 0x{pc:08X}")
    t.resume()
