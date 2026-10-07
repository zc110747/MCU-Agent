"""Read the OSPI globals over SWD - bypasses the console entirely.

Symbols from:
    arm-none-eabi-nm build/firmware.elf | grep g_ospi

The console has been unreliable, so this is the authoritative readout of what
bsp_ospi_init() and bsp_ospi_bitbang_id() actually stored.
"""
import sys
import time
from pyocd.core.helpers import ConnectHelper
from pyocd.core.memory_map import DeviceRegion, FlashRegion, MemoryMap, RamRegion

ID_ADDR       = 0x22038720
PROBED_ADDR   = 0x22038723
CAPACITY_ADDR = 0x22038724
READY_ADDR    = 0x2203871C


def build_map():
    flm = "tools/flash/RA8D1_2M.FLM"
    flash = FlashRegion(start=0x02000000, length=0x1F8000, flm=flm, name="code_flash")
    flash._RAMstart = 0x22000000
    flash._RAMsize = 0x7800
    sram = RamRegion(start=0x22000000, length=0xE0000, name="sram")
    periph = DeviceRegion(start=0x40000000, length=0x20000000, name="peripherals")
    return MemoryMap([flash, sram, periph])


def dump(t, label):
    idb = t.read_memory_block8(ID_ADDR, 3)
    probed = t.read8(PROBED_ADDR)
    ready = t.read8(READY_ADDR)
    cap = t.read32(CAPACITY_ADDR)
    print(f"{label}")
    print(f"  g_ospi_id       = {idb[0]:02X} {idb[1]:02X} {idb[2]:02X}")
    print(f"  g_ospi_probed   = {probed}")
    print(f"  g_ospi_ready    = {ready}")
    print(f"  g_ospi_capacity = {cap} (0x{cap:08X})")


s = ConnectHelper.session_with_chosen_probe(target_override="cortex_m")
with s:
    t = s.target
    t.memory_map = build_map()
    t.halt()
    dump(t, "current state (firmware running):")
    t.resume()
