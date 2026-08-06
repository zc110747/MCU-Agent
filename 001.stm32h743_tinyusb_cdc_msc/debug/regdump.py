#!/usr/bin/env python3
"""Dump a window of OV5640 registers over SWD.

    python debug/regdump.py 0x3800 [0x5000 ...]

Reads 64 consecutive SCCB registers per base address through the firmware's
cam_regdump hook and annotates the ones that matter for DVP timing.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPENOCD = os.environ.get("OPENOCD", r"D:/Software/openocd/bin/openocd.exe")
SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", r"D:/Software/openocd/share/openocd/scripts")
CFG = os.path.join(ROOT, "debug", "openocd.cfg").replace("\\", "/")
ELF = os.path.join(ROOT, "build", "stm32h743_uvc.elf")

ANNOT = {
    0x3800: "HS  ISP input window x start [11:8]",
    0x3801: "HS  ISP input window x start [7:0]",
    0x3802: "VS  ISP input window y start [11:8]",
    0x3803: "VS  ISP input window y start [7:0]",
    0x3804: "HW  ISP input window x end [11:8]",
    0x3805: "HW  ISP input window x end [7:0]",
    0x3806: "VH  ISP input window y end [11:8]",
    0x3807: "VH  ISP input window y end [7:0]",
    0x3808: "DVP output width  [11:8]",
    0x3809: "DVP output width  [7:0]",
    0x380A: "DVP output height [11:8]",
    0x380B: "DVP output height [7:0]",
    0x380C: "HTS total horizontal size [11:8]",
    0x380D: "HTS total horizontal size [7:0]",
    0x380E: "VTS total vertical size [11:8]",
    0x380F: "VTS total vertical size [7:0]",
    0x3810: "H offset [11:8]",
    0x3811: "H offset [7:0]",
    0x3812: "V offset [11:8]",
    0x3813: "V offset [7:0]",
    0x3814: "X INC  <<< [7:4] odd / [3:0] even horizontal sample increment",
    0x3815: "Y INC  <<< [7:4] odd / [3:0] even vertical sample increment",
    0x3820: "flip / vertical binning",
    0x3821: "mirror / horizontal binning",
    0x5001: "ISP control: bit5 scale, bit0 SDE  <<<",
    0x5000: "ISP control: bit7 LENC, bit5 raw gamma, bit1 BPC, bit0 WPC",
    0x501F: "format mux control",
    0x4300: "format control: 0x30 = YUV422 YUYV",
    0x503D: "test pattern: 0x80 = colour bars",
    0x4740: "VSYNC/HREF/PCLK polarity",
    0x3008: "system ctrl: bit6 sw power down",
    0x3035: "SC PLL ctrl1: [7:4] sysdiv [3:0] scale div",
    0x3036: "SC PLL ctrl2: multiplier",
    0x3037: "SC PLL ctrl3: bit4 root div, [3:0] pre-div",
    0x3108: "SC PLL ctrl: pclk root divider",
}


def symbols(*names):
    txt = subprocess.run(["arm-none-eabi-nm", ELF], capture_output=True, text=True).stdout
    t = {}
    for line in txt.splitlines():
        p = line.split()
        if len(p) == 3:
            t[p[2]] = int(p[0], 16)
    return [t[n] for n in names]


def main():
    bases = [int(a, 0) for a in sys.argv[1:]] or [0x3800]
    base_a, req, done, buf = symbols(
        "cam_regdump_base", "cam_regdump_req", "cam_regdump_done", "cam_regdump")

    cmds = [OPENOCD, "-s", SCRIPTS, "-f", CFG, "-c", "init",
            "-c", "reset run", "-c", "sleep 1500"]
    for i, b in enumerate(bases):
        cmds += ["-c", f"mwh 0x{base_a:08x} 0x{b:04x}",
                 "-c", f"mww 0x{req:08x} 1", "-c", "sleep 700",
                 "-c", f"echo {{===BASE 0x{b:04x}}}",
                 "-c", f"mdb 0x{buf:08x} 64"]
    cmds += ["-c", "shutdown"]

    out = subprocess.run(cmds, capture_output=True, text=True)
    text = out.stdout + out.stderr

    blocks = re.split(r"===BASE (0x[0-9a-f]+)", text)
    if len(blocks) < 3:
        sys.stderr.write(text)
        raise SystemExit("no register data returned")

    for i in range(1, len(blocks), 2):
        base = int(blocks[i], 16)
        body = blocks[i + 1]
        vals = []
        for line in body.splitlines():
            m = re.match(r"\s*0x[0-9a-f]+:\s+((?:[0-9a-f]{2}\s*)+)$", line)
            if m:
                vals += [int(x, 16) for x in m.group(1).split()]
        if not vals:
            continue
        print(f"\n===== OV5640 0x{base:04X} .. 0x{base + len(vals) - 1:04X} =====")
        for k, v in enumerate(vals[:64]):
            reg = base + k
            note = ANNOT.get(reg, "")
            if note:
                print(f"  0x{reg:04X} = 0x{v:02X}  {v:3d}   {note}")
        # compact hex block for the rest
        print("  raw:", " ".join(f"{v:02x}" for v in vals[:64]))


if __name__ == "__main__":
    main()
