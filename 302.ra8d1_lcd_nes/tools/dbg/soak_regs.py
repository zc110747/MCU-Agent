"""soak_regs.py - let the target RUN, then halt and dump registers.

read_regs.py resets and halts immediately (reset_and_halt), so it only ever
sees the state before bsp_pin_init()/bsp_ospi_init() run. That is exactly why
the first readback showed PSEL=0 everywhere: it was the pre-init state.

This script resets, resumes, waits for the firmware to reach its shell, then
halts and dumps the same registers. Whatever is still zero afterwards is a
real configuration gap, not a timing artefact.

Usage:
    python tools/dbg/soak_regs.py [seconds] [group]
      seconds : how long to let the board run (default 3)
      group   : pfs | mstp | xspi | all (default all)
"""

import sys
import time

from pyocd.core.helpers import ConnectHelper
from pyocd.core.memory_map import DeviceRegion, FlashRegion, MemoryMap, RamRegion

import read_regs as R


def build_map():
    return R.build_map()


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
    want = sys.argv[2] if len(sys.argv) > 2 else "all"

    session = ConnectHelper.session_with_chosen_probe(target_override="cortex_m")
    if session is None:
        print("no probe found")
        return 2

    with session:
        target = session.target
        target.memory_map = build_map()

        target.reset_and_halt()
        print(f"reset + halt, state={target.get_state()}")
        target.resume()
        print(f"resumed, letting firmware run for {secs:.1f}s ...")
        time.sleep(secs)
        target.halt()
        print(f"halted, state={target.get_state()}")
        # Read PC to see where we stopped.
        try:
            pc = target.read_core_register("pc")
            lr = target.read_core_register("lr")
            sp = target.read_core_register("sp")
            print(f"PC=0x{pc:08X} LR=0x{lr:08X} SP=0x{sp:08X}")
        except Exception as exc:  # noqa: BLE001
            print(f"core register read failed: {exc}")
        print()

        if want in ("all", "pfs"):
            R.dump_pfs(target)
        if want in ("all", "mstp"):
            R.dump_mstp(target)
        if want in ("all", "xspi"):
            R.dump_xspi(target)

        target.resume()
    return 0


if __name__ == "__main__":
    sys.exit(main())
