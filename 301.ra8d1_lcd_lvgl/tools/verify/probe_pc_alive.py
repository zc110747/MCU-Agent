#!/usr/bin/env python3
"""Is the CPU actually executing? Halt, sample PC twice, compare."""
from pyocd.core.helpers import ConnectHelper

with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    t.halt()
    pc1 = t.read_core_register("pc")
    xpsr = t.read_core_register("xpsr")
    t.resume()
    import time; time.sleep(0.3)
    t.halt()
    pc2 = t.read_core_register("pc")
    t.resume()
    print(f"PC1={pc1:#010x} xPSR={xpsr:#010x}")
    print(f"PC2={pc2:#010x}  -> {'RUNNING (PC advances)' if pc1 != pc2 else 'STUCK (PC frozen)'}")
