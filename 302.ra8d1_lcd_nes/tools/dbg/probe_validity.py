from pyocd.core.helpers import ConnectHelper
from pyocd.core.memory_map import DeviceRegion, FlashRegion, MemoryMap, RamRegion
import read_regs as R

s = ConnectHelper.session_with_chosen_probe(target_override="cortex_m")
if s is None:
    print("no probe"); raise SystemExit(2)
with s:
    t = s.target
    t.memory_map = R.build_map()
    t.reset_and_halt(); t.resume()
    import time; time.sleep(3); t.halt()

    checks = [
        ("XSPI VERSTT",     0x40268000 + 0x180),
        ("XSPI COMSTT",     0x40268000 + 0x184),
        ("MSTP MSTPCRB",    0x40203000 + 0x04),
        ("PFS P107",        0x40400800 + 1*0x40 + 7*4),
        ("ICU IRQCR",       0x40200000),
        ("SCB CPACR(part)", 0xE000ED88),
        ("SYST_CSR",        0xE000E010),
        ("GPIO PORTPR1",    0x40400000 + 1*4),
    ]
    for name, addr in checks:
        try:
            v = t.read32(addr)
            print(f"{name:22} 0x{addr:08X} = 0x{v:08X}")
        except Exception as e:
            print(f"{name:22} 0x{addr:08X} = ERR {e}")
    t.resume()
