#!/usr/bin/env python3
"""Flash programming tool for RA8D1 Vision Board via pyOCD (CMSIS-DAP).

Uses a generic cortex_m target (pack target init is broken on ART-Link)
plus the official Renesas flash algorithm loaded from the DFP FLM file.

Usage:
    python flash.py build/firmware.elf
    python flash.py build/firmware.bin --address 0x02000000
    python flash.py --erase-all
"""
import argparse
import sys
from pathlib import Path

from pyocd.core.session import Session
from pyocd.core.memory_map import MemoryMap, FlashRegion, RamRegion
from pyocd.flash.file_programmer import FileProgrammer

FLASH_START = 0x02000000
FLASH_LENGTH = 0x1F8000
SRAM_START = 0x22000000
SRAM_LENGTH = 0xE0000
ALGO_RAM_START = 0x22000000
ALGO_RAM_SIZE = 0x7800

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FLM = PROJECT_ROOT / "tools" / "flash" / "RA8D1_2M.FLM"


def build_session(args):
    """Open a pyOCD session with generic cortex_m target + RA8D1 flash region."""
    from pyocd.probe.aggregator import DebugProbeAggregator

    probes = DebugProbeAggregator.get_all_connected_probes()
    if not probes:
        sys.exit("no debug probe found")
    if args.serial:
        probe = next((p for p in probes if p.unique_id == args.serial), None)
        if probe is None:
            sys.exit(f"probe with id {args.serial} not found")
    else:
        probe = probes[0]
    print(f"Using probe: {probe.description} [{probe.unique_id}]")

    session = Session(
        probe,
        target_override="cortex_m",
        frequency=args.frequency,
        options={"no_config": True},
    )
    session.open()

    target = session.target
    flash_region = FlashRegion(
        start=FLASH_START,
        length=FLASH_LENGTH,
        flm=str(args.flm),
        name="code_flash",
    )
    # RAM range used by the flash algorithm (from DFP: RAMstart/RAMsize)
    flash_region._RAMstart = ALGO_RAM_START
    flash_region._RAMsize = ALGO_RAM_SIZE

    sram_region = RamRegion(start=SRAM_START, length=SRAM_LENGTH, name="sram")
    target.memory_map = MemoryMap([flash_region, sram_region])

    # Finalise: load FLM and build the algo dict
    from pyocd.target.pack.flm_region_builder import FlmFlashRegionBuilder

    builder = FlmFlashRegionBuilder(target, target.memory_map)
    if not builder.finalise_region(flash_region):
        raise RuntimeError("failed to load flash algorithm from FLM")

    # Attach the Flash instance (normally done during target connect)
    from pyocd.flash.flash import Flash

    flash_obj = Flash(target, flash_region.algo)
    flash_obj.region = flash_region
    flash_region.flash = flash_obj

    return session, flash_region


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", nargs="?", help="image file: .elf/.hex/.bin")
    parser.add_argument("--address", type=lambda x: int(x, 0), default=None,
                        help="base address for raw binary image")
    parser.add_argument("--flm", default=str(DEFAULT_FLM), help="flash algorithm FLM file")
    parser.add_argument("--serial", default=None, help="debug probe unique id")
    parser.add_argument("--frequency", type=int, default=1000000, help="SWD clock in Hz")
    parser.add_argument("--erase-all", action="store_true", help="erase entire chip first")
    parser.add_argument("--no-reset", action="store_true", help="do not reset after programming")
    args = parser.parse_args()

    if not Path(args.flm).is_file():
        sys.exit(f"FLM not found: {args.flm} (run tools/flash/get_pack.sh first)")
    if not args.erase_all and not args.image:
        sys.exit("no image specified (or use --erase-all)")

    session, flash_region = build_session(args)
    try:
        # Reset+halt before flash ops: puts the core in a clean state with
        # I/D-cache off (a running app with D-cache on breaks algo RAM loading)
        session.target.reset_and_halt()

        if args.erase_all:
            print("Erasing entire chip...")
            with flash_region.flash as f:
                f.init(flash_region.flash.OP_ERASE_ALL)
                f.erase_all()
                f.cleanup()
            print("Erase done.")

        if args.image:
            print(f"Programming {args.image} ...")
            programmer = FileProgrammer(session)
            programmer.program(args.image, address=args.address, verify=True)
            print("Program done.")

        if not args.no_reset:
            session.target.reset()
            print("Target reset and running.")
    finally:
        session.close()


if __name__ == "__main__":
    main()
