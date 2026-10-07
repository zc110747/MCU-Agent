#!/usr/bin/env python3
"""Read RA8D1 peripheral registers over SWD in one session - OSPI diagnosis.

Reads the PFS (pin function select) registers for the six OSPI pins, the MSTP
gate for the xSPI block and the main xSPI registers, so the pin mux and module
clock state can be established without a scope.

Usage:
    python tools/verify/probe_ospi_regs.py
"""
import sys
from pathlib import Path

from pyocd.core.memory_map import DeviceRegion, FlashRegion, MemoryMap, RamRegion
from pyocd.core.session import Session
from pyocd.probe.aggregator import DebugProbeAggregator

PROJECT_ROOT = Path(__file__).resolve().parents[2]
FLM_PATH = PROJECT_ROOT / "tools" / "flash" / "RA8D1_2M.FLM"

FLASH_START = 0x02000000
FLASH_LENGTH = 0x1F8000
SRAM_START = 0x22000000
SRAM_LENGTH = 0xE0000
PERIPH_START = 0x40000000
PERIPH_LENGTH = 0x20000000

PFS_BASE = 0x40400800
XSPI_BASE = 0x40268000
# R_MSTP: MSTPCRA at +0x00, MSTPCRB at +0x04.
MSTP_BASE = 0x40203000
MSTPCRB_OFF = 0x04


def pfs(port, pin):
    """PFS address for PmnPFS.

    Layout (R7FA8D1BH.h): R_PFS is an array of 15 R_PFS_PORT_Type, each 0x40
    bytes holding 16 R_PFS_PORT_PIN_Type of 4 bytes.  So the address is
    PFS_BASE + port*0x40 + pin*4 - the flat "port*4 + pin + 3" table used by
    the older RA family does NOT apply here.
    """
    return PFS_BASE + port * 0x40 + pin * 4


OSPI_PINS = [
    ("P107 CS0 ", 1, 7),
    ("P808 SCLK", 8, 8),
    ("P100 SIO0", 10, 0),
    ("P803 SIO1", 8, 3),
    ("P103 SIO2", 10, 3),
    ("P101 SIO3", 10, 1),
]

XSPI_REGS = [
    ("WRAPCFG  ", 0x00),
    ("COMCFG   ", 0x04),
    ("BMCFGCH0 ", 0x08),
    ("LIOCFGCS0", 0x50),
    ("BMCTL0   ", 0x60),
    ("CMCTLCH0 ", 0x68),
    ("CDCTL0   ", 0x70),
    ("COMSTT   ", 0x90),
]


def build_session():
    probes = DebugProbeAggregator.get_all_connected_probes()
    if not probes:
        sys.exit("no debug probe found")
    probe = probes[0]

    flash_region = FlashRegion(start=FLASH_START, length=FLASH_LENGTH,
                               flm=str(FLM_PATH), name="code_flash")
    flash_region._RAMstart = SRAM_START
    flash_region._RAMsize = 0x7800
    memmap = MemoryMap([
        flash_region,
        RamRegion(start=SRAM_START, length=SRAM_LENGTH, name="sram"),
        DeviceRegion(start=PERIPH_START, length=PERIPH_LENGTH, name="peripherals"),
    ])

    session = Session(probe, target_override="cortex_m", frequency=1000000,
                      options={"no_config": True})
    session.target.memory_map = memmap
    session.open()
    return session


def main():
    session = build_session()
    target = session.target
    target.halt()

    try:
        print("== PFS (pin function select) ==")
        print(f"{'pin':10s} {'addr':12s} {'value':10s} PSEL  (OSPI=0x1c)")
        for name, port, pin in OSPI_PINS:
            addr = pfs(port, pin)
            val = target.read32(addr)
            psel = (val >> 24) & 0x3F
            flag = "  <-- OSPI" if psel == 0x1C else ""
            print(f"{name:10s} {addr:#012x} {val:#010x} {psel:#04x}{flag}")

        print()
        print("== xSPI registers ==")
        for name, off in XSPI_REGS:
            val = target.read32(XSPI_BASE + off)
            print(f"{name:10s} {XSPI_BASE + off:#010x}  {val:#010x}")

        print()
        print("== MSTP ==")
        mstpb = target.read32(MSTP_BASE + MSTPCRB_OFF)
        mstpc = target.read32(MSTP_BASE + MSTPCRB_OFF + 4)
        xspi_stopped = (mstpb >> 16) & 1
        print(f"MSTPCRB {MSTP_BASE + MSTPCRB_OFF:#010x}  {mstpb:#010x}  "
              f"bit16(xSPI)={'STOPPED' if xspi_stopped else 'running'}")
        print(f"MSTPCRC {MSTP_BASE + MSTPCRB_OFF + 4:#010x}  {mstpc:#010x}")
    finally:
        target.resume()
        session.close()


if __name__ == "__main__":
    main()
