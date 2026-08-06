#!/usr/bin/env python3
"""Trigger the firmware's HSYNC-bucketed DVP data-bus probe and report it.

    python debug/hrefprobe.py

Samples D[7:0] together with HSYNC/VSYNC for many whole lines and reports,
separately for the HSYNC-high and HSYNC-low windows, how many distinct byte
values appear plus a few of the values seen, and how many sync edges occurred.

  both buckets constant  -> sensor is not streaming real pixel data
  only one bucket varies -> the DCMI HSPolarity selects the wrong window
"""

import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPENOCD = os.environ.get("OPENOCD", r"D:/Software/openocd/bin/openocd.exe")
SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", r"D:/Software/openocd/share/openocd/scripts")
CFG = os.path.join(ROOT, "debug", "openocd.cfg").replace("\\", "/")
ELF = os.path.join(ROOT, "build", "stm32h743_uvc.elf")


def syms():
    out = subprocess.run(["arm-none-eabi-nm", ELF], capture_output=True,
                         text=True, check=True).stdout
    want = {"cam_href_req", "cam_bus_hi_samples", "cam_bus_lo_samples",
            "cam_bus_hi_distinct", "cam_bus_lo_distinct", "cam_bus_hi_count",
            "cam_bus_lo_count", "cam_href_edges", "cam_vsync_edges",
            "cam_probe_done"}
    return {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines())
            if len(p) == 3 and p[2] in want}


def ocd(*cmds):
    argv = [OPENOCD, "-s", SCRIPTS, "-f", CFG, "-c", "init"]
    for c in cmds:
        argv += ["-c", c]
    argv += ["-c", "shutdown"]
    p = subprocess.run(argv, capture_output=True, text=True)
    return p.stdout + p.stderr


def read_words(blob):
    vals = {}
    for m in re.finditer(r"0x([0-9a-fA-F]{8}):((?:\s+[0-9a-fA-F]{8})+)", blob):
        base = int(m.group(1), 16)
        for i, w in enumerate(m.group(2).split()):
            vals[base + 4 * i] = int(w, 16)
    return vals


def bytes_from(vals, base, n):
    out = []
    for i in range(0, (n + 3) // 4):
        w = vals.get(base + 4 * i, 0)
        out += [(w >> sh) & 0xFF for sh in (0, 8, 16, 24)]
    return out[:n]


def main():
    s = syms()
    missing = [k for k in ("cam_href_req", "cam_bus_hi_samples",
                           "cam_bus_lo_samples", "cam_bus_hi_distinct",
                           "cam_bus_lo_distinct", "cam_bus_hi_count",
                           "cam_bus_lo_count", "cam_href_edges",
                           "cam_vsync_edges", "cam_probe_done") if k not in s]
    if missing:
        raise SystemExit(f"missing symbols: {missing}")

    before = read_words(ocd(f"mdw 0x{s['cam_probe_done']:08x} 1"))
    done0 = before.get(s["cam_probe_done"], 0)

    ocd(f"mww 0x{s['cam_href_req']:08x} 1")
    time.sleep(1.0)

    blob = ocd(f"mdw 0x{s['cam_probe_done']:08x} 1",
               f"mdw 0x{s['cam_bus_hi_count']:08x} 1",
               f"mdw 0x{s['cam_bus_lo_count']:08x} 1",
               f"mdw 0x{s['cam_bus_hi_distinct']:08x} 1",
               f"mdw 0x{s['cam_bus_lo_distinct']:08x} 1",
               f"mdw 0x{s['cam_href_edges']:08x} 1",
               f"mdw 0x{s['cam_vsync_edges']:08x} 1",
               f"mdw 0x{s['cam_bus_hi_samples']:08x} 4",
               f"mdw 0x{s['cam_bus_lo_samples']:08x} 4")
    v = read_words(blob)

    if v.get(s["cam_probe_done"], 0) == done0:
        raise SystemExit("probe never ran - is bsp_camera_service() being called?")

    hi_count = v.get(s["cam_bus_hi_count"], 0)
    lo_count = v.get(s["cam_bus_lo_count"], 0)
    hi_dist = v.get(s["cam_bus_hi_distinct"], 0)
    lo_dist = v.get(s["cam_bus_lo_distinct"], 0)
    href_edges = v.get(s["cam_href_edges"], 0)
    vsync_edges = v.get(s["cam_vsync_edges"], 0)
    hi_s = bytes_from(v, s["cam_bus_hi_samples"], 16)
    lo_s = bytes_from(v, s["cam_bus_lo_samples"], 16)

    print(f"HSYNC edges = {href_edges}   VSYNC edges = {vsync_edges}")
    print(f"  samples while HSYNC high : {hi_count:7d}   distinct = {hi_dist}")
    print(f"  samples while HSYNC low  : {lo_count:7d}   distinct = {lo_dist}")
    print(f"  HSYNC-high first 16 bytes : " + " ".join(f"{b:02x}" for b in hi_s))
    print(f"  HSYNC-low  first 16 bytes : " + " ".join(f"{b:02x}" for b in lo_s))

    print()
    if hi_dist <= 1 and lo_dist <= 1:
        print("VERDICT: bus is constant in BOTH windows.")
        print("  -> the sensor is not emitting pixel data; blame its timing/sync")
        print("     config (PCLK gating, output format/bit-width, or HREF/HSYNC mode).")
    elif hi_dist > 1 and lo_dist <= 1:
        print("VERDICT: bus only varies while HSYNC is HIGH.")
        print("  -> the DCMI HSPolarity should capture the HSYNC-HIGH window;")
        print("     check DCMI_HSPolarity vs the sensor's HREF polarity.")
    elif lo_dist > 1 and hi_dist <= 1:
        print("VERDICT: bus only varies while HSYNC is LOW.")
        print("  -> the DCMI HSPolarity is inverted; the active window is HSYNC-LOW.")
    else:
        print("VERDICT: bus varies in BOTH windows - sensor is streaming real data.")
        print("  -> the DCMI sampling (PCLK polarity / data width) is at fault.")


if __name__ == "__main__":
    main()
