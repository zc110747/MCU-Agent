#!/usr/bin/env python3
"""Full-block dump of GPT7 (0x00..0xFC) + POEG + MSTPCRC for offline diff."""
import json
import sys

from pyocd.core.helpers import ConnectHelper

GPT7    = 0x40322700
POEG0   = 0x400B4000    # POEG group 0 (verify below if reads fail)
MSTPCRC = 0x4001F008    # from R_MSTP base 0x4001F000 + 0x8

tag = sys.argv[1] if len(sys.argv) > 1 else "fw"
out = {}
with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                             frequency=4000000,
                                             halt_on_connect=False) as session:
    t = session.target
    for off in range(0x00, 0x100, 4):
        try:
            out[f"GPT7+{off:#04x}"] = t.read32(GPT7 + off)
        except Exception as e:
            out[f"GPT7+{off:#04x}"] = f"ERR {e}"
    for name, addr in (("POEG0", POEG0), ("MSTPCRC", MSTPCRC)):
        try:
            out[name] = t.read32(addr)
        except Exception as e:
            out[name] = f"ERR {e}"

with open(f"E:/cnb/git/MCU-Agent/301.ra8d1_lcd_lvgl/tools/verify/gpt7_dump_{tag}.json", "w") as f:
    json.dump(out, f, indent=1)
print(f"saved gpt7_dump_{tag}.json ({len(out)} entries)")
