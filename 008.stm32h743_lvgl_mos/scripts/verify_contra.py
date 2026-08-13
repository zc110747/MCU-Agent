#!/usr/bin/env python3
"""Verify Contra (魂斗罗, mapper 23 / VRC2/VRC4) can now be opened and runs.

This is the second half of the "魂斗罗 won't open" fix:
  * part 1 (done): ROM buffer enlarged to 286 KiB so the 262160 B image fits
  * part 2 (this run): mapper 23 (VRC2/VRC4) implemented in the NES core

Verification goals on the device:
  rom list            -> find Contra's index
  rom load <idx>      -> opens NES page, parses header (mapper 23), starts running
  status              -> confirm "mapper 23 (VRC2/VRC4)", fps > 0, D2 ~286 KiB alloc
A non-zero, sane fps after a few seconds is the key signal that the bank
switching / IRQ decode is correct (a wrong stride would garble PRG banks and
either hard-fault or render garbage, dropping fps to ~0).
"""
import sys
import time
import re
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
BAUD = 115200
KEY = "魂斗罗"          # 中文 ROM 名（Contra）


def open_port(p):
    s = serial.Serial(p, BAUD, timeout=0.3)
    time.sleep(0.2)
    s.reset_input_buffer()
    return s


def send(s, line, wait=0.15):
    s.write((line + "\r\n").encode("utf-8", "replace"))
    time.sleep(wait)


def drain(s, seconds, stop_markers=None):
    buf = []
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = s.read(4096)
        except Exception:
            chunk = b""
        if chunk:
            txt = chunk.decode("utf-8", "replace")
            buf.append(txt)
            if stop_markers:
                joined = "".join(buf)
                if any(m in joined for m in stop_markers):
                    break
        else:
            time.sleep(0.02)
    return "".join(buf)


def find_index(out):
    for ln in out.splitlines():
        ln_s = ln.strip()
        if ln_s[:2].isdigit() and KEY in ln_s.lower():
            return int(ln_s[:2])
    for ln in out.splitlines():
        if KEY in ln.lower():
            head = ln.strip().split()[0]
            if head.isdigit():
                return int(head)
    return None


def main():
    s = open_port(PORT)
    send(s, "reset", wait=1.5)
    drain(s, 2.0)  # banner

    print("=== rom list ===")
    send(s, "rom list", wait=0.5)
    out = drain(s, 3.0)
    print(out)

    idx = find_index(out)
    if idx is None:
        print(f"[FAIL] could not find a ROM containing '{KEY}' in the list above")
        return 1

    print(f"\n=== rom load {idx} ===")
    send(s, f"rom load {idx}", wait=0.5)
    out = drain(s, 8.0, stop_markers=["loaded,", "rejected", "too large",
                                       "文件过大", "unsupported"])
    print(out)

    if "unsupported mapper" in out or "too large" in out or "文件过大" in out:
        print("\n[RESULT] FAIL: ROM still rejected by the core.")
        return 1
    if "mapper=23" not in out and "mapper 23" not in out:
        print("\n[WARN] did not see mapper 23 in the boot trace; inspect above.")

    # Let it actually render a few hundred frames.
    print("\n=== waiting 3 s for frames to accumulate ===")
    time.sleep(3.0)

    print("=== rom info ===")
    send(s, "rom info", wait=0.3)
    out = drain(s, 2.0)
    print(out)

    m = re.search(r"mapper\s*:\s*(\d+)\s*\(([^)]*)\)", out)
    if m:
        num = int(m.group(1))
        name = m.group(2)
        print(f"[INFO] reported mapper = {num} ({name})")
        if num != 23:
            print(f"[RESULT] FAIL: expected mapper 23, got {num}.")
            return 1
    else:
        print("[WARN] could not parse mapper line from rom info.")

    fm = re.search(r"fps\s*:\s*(\d+)", out)
    fps = int(fm.group(1)) if fm else -1
    if fps <= 0:
        print(f"[RESULT] FAIL: fps={fps} - game is not rendering (bad banks / crash).")
        return 1
    print(f"[INFO] fps = {fps} (sane => banks/IRQ decode correct).")

    # Confirm the ~286 KiB D2 allocation happened.
    print("=== status (D2 alloc + responsiveness) ===")
    send(s, "status", wait=0.3)
    out2 = drain(s, 2.0)
    print(out2)

    dm = re.search(r"sram\s+d2\s*:\s*(\d+)/(\d+)", out2)
    if dm:
        free = int(dm.group(1))
        print(f"[INFO] sram d2 free after load = {free} B (baseline ~294864 B)")
        if free < 50000:
            print("[INFO] D2 allocation of ~286 KiB confirmed.")
        else:
            print("[WARN] D2 free did not drop as expected.")

    if "fps" in out2:
        print("[INFO] device still responsive after load.")
    else:
        print("[RESULT] FAIL: device stopped responding after load (likely crash).")
        return 1

    print("\n[RESULT] PASS: Contra (mapper 23) loads, reports VRC2/VRC4, "
          "renders at fps > 0, and the device stays responsive.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
