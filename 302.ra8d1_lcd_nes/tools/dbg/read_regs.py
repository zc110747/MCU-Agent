"""read_regs.py - dump RA8D1 PFS / MSTP / XSPI registers over SWD.

Purpose: get hardware evidence for the "OSPI JEDEC ID reads back 00 00 00"
problem without a scope. Three questions to answer:

  1. Are the six OSPI pins actually muxed?  -> PFS PmnPFS.PSEL == 0x1C, PMR == 1
  2. Is the XSPI peripheral clock enabled?   -> MSTPCRB bit16 == 0
  3. What does the XSPI controller think?    -> COMSTT / BMCTL0 / CMCTLCH / CDBUF

Usage:
    python tools/dbg/read_regs.py            # all three groups
    python tools/dbg/read_regs.py pfs        # only PFS
    python tools/dbg/read_regs.py xspi       # only XSPI

The target must be halted (or at least running) - we read via the debug
probe, not by running code on the board.
"""

import sys

from pyocd.core.helpers import ConnectHelper
from pyocd.core.memory_map import DeviceRegion, FlashRegion, MemoryMap, RamRegion

PFS_BASE = 0x40400800
MSTP_BASE = 0x40203000
XSPI_BASE = 0x40268000

# ============================================================================
# IMPORTANT: DO NOT read the peripheral registers below over SWD and treat the
# result as evidence.
#
# Measured on this board: a pyOCD session (connect / halt / read) reports
# PmnPFS = 0x00000000 and MSTPCRB = 0xffffffff even while the running firmware
# reads back 0x1C010C00 / 0xffbeffff from the very same addresses.  The session
# reads are simply not the peripheral's state, so "the mux was never applied"
# and "the xSPI clock is gated" would both be false conclusions drawn from a
# bad instrument.
#
# The authoritative view of the pin mux and the xSPI block is the firmware
# itself: `ospi pfs` and `ospi regs` over the console.  The PFS/MSTP sections
# of this script are kept for the RAM/scan use cases only, and print a warning.
# ============================================================================

# OSPI pins, confirmed against the board schematic:
#   CS1 = P104, SCLK = P808, SIO0 = P100 (MOSI), SIO1 = P803 (MISO),
#   SIO2 = P103 (WP), SIO3 = P101 (HOLD/RESET)
# The part hangs off chip-select 1, so the driver opens as
# OSPI_B_DEVICE_NUMBER_1 and the XiP window is 0x90000000 (CS0 would be
# 0x80000000).  An earlier revision of this file listed CS0 on P107, taken from
# the reference project's generator output, which is a different pin.
OSPI_PINS = [
    ("CS1   ", 10, 4),   # P104  <-- the one actually fitted
    ("SCLK  ", 8, 8),    # P808
    ("SIO0  ", 1, 0),    # P100  MOSI
    ("SIO1  ", 8, 3),    # P803  MISO
    ("SIO2  ", 1, 3),    # P103  WP
    ("SIO3  ", 1, 1),    # P101  HOLD/RESET
]

PSEL_OSPI = 0x1C  # IOPORT_PERIPHERAL_OSPI = 0x1C << 24

# Offsets read straight out of the R_XSPI_Type member comments in
# R7FA8D1BH.h.  Two earlier revisions of this file were wrong in ways that
# silently read unrelated registers, so the authoritative lines are quoted:
#   CMCFGCS[2] @0x10 (stride 0x10)   LIOCFGCS[2] @0x50 (stride 4)
#   CDBUF[4]   @0x80 (stride 0x10)   LPCTL0/1 @0x100/0x104
#   CCCTLCS[2] @0x130 (stride 0x20)  VERSTT @0x180  COMSTT @0x184
#   CASTTCS[2] @0x188 (stride 0x20)  INTS @0x190  INTC @0x194  INTE @0x198
XSPI_REGS = [
    (0x000, "WRAPCFG  "),
    (0x004, "COMCFG   "),
    (0x008, "BMCFGCH0 "),
    (0x00C, "BMCFGCH1 "),
    (0x010, "CMCFGCS0 "),
    (0x020, "CMCFGCS1 "),
    (0x050, "LIOCFGCS0"),
    (0x054, "LIOCFGCS1"),
    (0x058, "ABMCFG   "),
    (0x060, "BMCTL0   "),
    (0x064, "BMCTL1   "),
    (0x068, "CMCTLCH0 "),
    (0x06C, "CMCTLCH1 "),
    (0x070, "CDCTL0   "),
    (0x080, "CDBUF0D0 "),
    (0x084, "CDBUF0D1 "),
    (0x090, "CDBUF1D0 "),
    (0x094, "CDBUF1D1 "),
    (0x100, "LPCTL0   "),
    (0x104, "LPCTL1   "),
    (0x108, "LIOCTL   "),
    (0x130, "CCCTLCS0 "),
    (0x150, "CCCTLCS1 "),
    (0x180, "VERSTT   "),
    (0x184, "COMSTT   "),
    (0x188, "CASTTCS0 "),
    (0x1A8, "CASTTCS1 "),
    (0x190, "INTS     "),
    (0x194, "INTC     "),
    (0x198, "INTE     "),
]


def build_map():
    flm = "tools/flash/RA8D1_2M.FLM"
    flash = FlashRegion(start=0x02000000, length=0x1F8000, flm=flm, name="code_flash")
    flash._RAMstart = 0x22000000
    flash._RAMsize = 0x7800
    sram = RamRegion(start=0x22000000, length=0xE0000, name="sram")
    periph = DeviceRegion(start=0x40000000, length=0x20000000, name="peripherals")
    return MemoryMap([flash, sram, periph])


def pin_addr(port, pin):
    return PFS_BASE + port * 0x40 + pin * 4


def dump_pfs(target):
    print("=== PFS: OSPI pin mux ===")
    print("WARNING: SWD reads of PmnPFS are NOT reliable on this board - a debug")
    print("         session reports 0x00000000 for pins the firmware reads as")
    print("         0x1C010C00.  Use `ospi pfs` over the console instead; the")
    print("         output below is not evidence either way.")
    print(f"PFS base = 0x{PFS_BASE:08X}   (expected PSEL=0x{PSEL_OSPI:02X}, PMR=1)")
    print(f"{'signal':8} {'pin':6} {'addr':12} {'PmnPFS':>10}  PSEL PMR PDR PCR ASEL")
    print("-" * 66)
    for name, port, pin in OSPI_PINS:
        addr = pin_addr(port, pin)
        val = target.read32(addr)
        psel = (val >> 24) & 0x1F
        pmr = (val >> 16) & 1
        pdr = (val >> 2) & 1
        pcr = (val >> 4) & 1
        asel = (val >> 15) & 1
        print(
            f"{name:8} P{port}{pin:<4} 0x{addr:08X} 0x{val:08X}   "
            f"{psel:<4} {pmr:<3} {pdr:<3} {pcr:<3} {asel}"
        )
    print("PFS verdict: UNRELIABLE - see warning above, use `ospi pfs`")
    return True


def dump_mstp(target):
    print()
    print("=== MSTP: XSPI clock gate ===")
    print("WARNING: same caveat as PFS - the firmware reads MSTPCRB as 0xffbeffff")
    print("         (XSPI bit16 clear = clock ON) while SWD reads 0xffffffff.")
    print(f"MSTP base = 0x{MSTP_BASE:08X}")
    mstpcra = target.read32(MSTP_BASE + 0x00)
    mstpcrb = target.read32(MSTP_BASE + 0x04)
    print(f"MSTPCRA = 0x{mstpcra:08X}")
    print(f"MSTPCRB = 0x{mstpcrb:08X}")
    bit = (mstpcrb >> 16) & 1
    print(f"MSTPCRB bit16 (XSPI) = {bit}   (NOT evidence; use `ospi regs`)")
    return True


def dump_xspi(target):
    print()
    print("=== XSPI controller ===")
    print(f"XSPI base = 0x{XSPI_BASE:08X}")
    for off, name in XSPI_REGS:
        val = target.read32(XSPI_BASE + off)
        print(f"  +0x{off:03X} {name} = 0x{val:08X}")


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else "all"

    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={"chip_erase": "sector"},
    )
    if session is None:
        print("no probe found")
        return 2

    with session:
        target = session.target
        target.memory_map = build_map()
        target.reset_and_halt()
        print(f"target halted: {target.get_state()}")
        print()

        if want in ("all", "pfs"):
            dump_pfs(target)
        if want in ("all", "mstp"):
            dump_mstp(target)
        if want in ("all", "xspi"):
            dump_xspi(target)

        target.resume()
    return 0


if __name__ == "__main__":
    sys.exit(main())
