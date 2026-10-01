#!/usr/bin/env python3
"""Panel-side forensics: is the GLCDC actually driving the panel?

Reads over SWD (no shell needed):
  - GLCDC BG.EN/HSIZE/VSIZE, SYSCNT.STMON, PANEL_CLK
  - GR[0] layer base + enable
  - panel reset P1104 / backlight P1011 PFS + PCNTR levels
  - P3xx RGB bus pins PSEL (are they muxed to GLCDC?)
"""
import time
from pyocd.core.helpers import ConnectHelper

R_GLCDC   = 0x40342000
R_PFS     = 0x40400800
PFS_STRIDE = 0x40
R_PORT0   = 0x40400000
PORT_STRIDE = 0x20
R_PMISC   = 0x40400D00

def u32(t, a): return t.read32(a)
def u16(t, a): return t.read16(a)

sess = ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                               frequency=1000000,
                                               halt_on_connect=False)
sess.__enter__()
t = sess.target
t.reset()
time.sleep(2.0)

print("=== GLCDC (base 0x40342000) ===")
bg = R_GLCDC + 0x0000
print(f"BG.EN      = 0x{t.read32(bg+0x00):08x}   (bit0 EN, bit8 VEN, bit16 SWRST)")
print(f"BG.HSIZE   = 0x{t.read32(bg+0x0C):08x}")
print(f"BG.VSIZE   = 0x{t.read32(bg+0x10):08x}")
stmon = t.read32(R_GLCDC + 0x100)
print(f"SYSCNT.STMON = 0x{stmon:08x}   (bit0 VPOS bit1 L1UNDF bit2 L2UNDF)")
pclk = t.read32(R_GLCDC + 0x124)
print(f"PANEL_CLK    = 0x{pclk:08x}   (DCDR/CLKEN/CLKSEL/PIXSEL)")

print("\n=== GR[0] layer 1 ===")
gr0 = R_GLCDC + 0x1100
print(f"GR0.FLMRD  = 0x{t.read32(gr0+0x000):08x}  (bit0 RENB?)")
print(f"GR0.FLM2   = 0x{t.read32(gr0+0x004):08x}  (0x68000000 expected)")
print(f"GR0.MON    = 0x{t.read32(gr0+0x100):08x}")

print("\n=== panel pins ===")
def pfs(port, pin):
    return R_PFS + port*PFS_STRIDE + pin*4

for (port, pin, name) in ((11, 4, "P1104 LCD reset"), (10, 11, "P1011 backlight")):
    v = t.read32(pfs(port, pin))
    podr = (v >> 0) & 1
    pidr = (v >> 1) & 1
    pdr  = (v >> 2) & 1
    pmr  = (v >> 16) & 1
    psel = (v >> 24) & 0x1F
    print(f"{name}: PFS=0x{v:08x} PODR={podr} PIDR={pidr} PDR(out)={pdr} PMR={pmr} PSEL={psel}")

# PCNTR: read the actual port state
for (port, name) in ((10, "Port10"), (11, "Port11")):
    base = R_PORT0 + port*PORT_STRIDE
    pcntr1 = t.read32(base + 0x00)
    pcntr2 = t.read32(base + 0x04)
    print(f"{name} PCNTR1(PODR/PIDR)=0x{pcntr1:08x} PCNTR2=0x{pcntr2:08x}")

sess.__exit__(None, None, None)
print("\nDONE")
