# pyocd_user.py - RA8D1 Vision Board target setup for pyOCD (auto-discovered).
#
# The DFP pack target (R7FA8D1BH) fails to init on the ART-Link CMSIS-DAP probe,
# so we use the generic "cortex_m" target and inject a memory map with the
# official Renesas flash algorithm (FLM from the Renesas RA DFP pack).
#
# This replicates what tools/flash/flash.py does, but declaratively, so that
# "pyocd flash" / "pyocd gdbserver" (and therefore VSCode cortex-debug) can
# program and debug the code flash at 0x02000000.
#
# Memory map values match tools/flash/flash.py (verified on hardware):
#   code flash : 0x02000000 .. 0x03F7FFFF  (0x1F8000, ~2MB)
#   algo RAM   : 0x22000000 .. 0x220077FF  (0x7800, from DFP RAMstart/RAMsize)
#   SRAM       : 0x22000000 .. 0x220DFFFF  (0xE0000, non-secure view)
import os

from pyocd.core.memory_map import DeviceRegion, FlashRegion, MemoryMap, RamRegion

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
FLM_PATH = os.path.join(_THIS_DIR, "tools", "flash", "RA8D1_2M.FLM")

FLASH_START = 0x02000000
FLASH_LENGTH = 0x1F8000
SRAM_START = 0x22000000
SRAM_LENGTH = 0xE0000
ALGO_RAM_START = 0x22000000
ALGO_RAM_SIZE = 0x7800
# Peripheral space (ICU/SCI/PFS/...) - read-only from gdb, needed to verify
# pin mux and UART status registers without a scope.
PERIPH_START = 0x40000000
PERIPH_LENGTH = 0x20000000
# External memory window: QSPI 0x60000000 + SDRAM 0x68000000 (LVGL framebuffer).
EXTMEM_START = 0x60000000
EXTMEM_LENGTH = 0x10000000


def _build_memory_map():
    flash_region = FlashRegion(
        start=FLASH_START,
        length=FLASH_LENGTH,
        flm=FLM_PATH,
        name="code_flash",
    )
    # RAM range used by the flash algorithm (from DFP: RAMstart/RAMsize).
    flash_region._RAMstart = ALGO_RAM_START
    flash_region._RAMsize = ALGO_RAM_SIZE

    sram_region = RamRegion(start=SRAM_START, length=SRAM_LENGTH, name="sram")
    periph_region = DeviceRegion(start=PERIPH_START, length=PERIPH_LENGTH, name="peripherals")
    extmem_region = RamRegion(start=EXTMEM_START, length=EXTMEM_LENGTH, name="ext_mem")
    return MemoryMap([flash_region, sram_region, periph_region, extmem_region])


def will_init_target(target, init_sequence):
    """Delegate hook: run before the target init sequence.

    Replacing the memory map here means the "create_flash" init task will
    load the FLM flash algorithm and build the Flash instance for us.
    """
    target.memory_map = _build_memory_map()
