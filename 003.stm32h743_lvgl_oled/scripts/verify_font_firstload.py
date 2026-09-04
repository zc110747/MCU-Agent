#!/usr/bin/env python3
"""Font-page first-load regression check.

The font page used to take ~200 ms longer on its FIRST paint than on subsequent
switches.  That was fixed by (a) preloading Latin glyphs too and (b) warming up
every page once at boot with the display flush suppressed.  This script locks
that win in: it captures the serial banner, parses the `[PAGE] switch -> N`
lines the firmware prints, and asserts that the FIRST font-page load is not
materially slower than the warm ones, and that it does zero glyph-cache misses
and zero SD reads.

Usage
-----
    # Flash the Release build, reset, capture 35 s, assert:
    python scripts/verify_font_firstload.py --flash

    # Just capture from an already-running board (no flash):
    python scripts/verify_font_firstload.py --seconds 35

    # Parse a previously saved capture instead of going live:
    python scripts/verify_font_firstload.py --in diag12.txt

Exit code is 0 on PASS, 1 on FAIL (CI friendly).
"""
import argparse
import os
import re
import subprocess
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is required:  pip install pyserial")


PAGE_RE = re.compile(
    r"\[PAGE\] switch -> (?P<idx>\d+), scr_load (?P<scr>\d+) us, refr (?P<refr>\d+) us, "
    r"bmp_miss\+(?P<bmp>\d+) hit\+(?P<hit>\d+) evict\+(?P<evict>\d+)"
    r"(?: \| ctf_sd\+(?P<ctf_sd>\d+) ttf_fill\+(?P<ttf_fill>\d+) ttf_read\+(?P<ttf_read>\d+) us)?"
)

BOOT_RE = re.compile(r"\[BOOT\] done: pending0=(?P<p0>\d+) pending=(?P<p>\d+) elapsed=(?P<e>\d+) engine=(?P<eng>\d+)")


def select_port(preferred):
    if preferred and preferred.lower() != "auto":
        return preferred
    ports = list(list_ports.comports())
    cand = [p for p in ports if p.description and
            ("STLink" in p.description or "STMicroelectronics" in p.description
             or "Virtual COM" in p.description)]
    if not cand:
        cand = ports
    if not cand:
        sys.exit("no serial ports found; pass --port explicitly")
    return cand[0].device


def capture(port, baud, seconds):
    """Open the port (so it is listening before any reset) and read for `seconds`."""
    chunks = []
    try:
        ser = serial.Serial(port, baud, timeout=0.2)
    except serial.SerialException as exc:
        sys.exit(f"cannot open {port}: {exc}")
    print(f"capturing {port} @ {baud} for {seconds}s")
    end = time.time() + seconds
    try:
        while time.time() < end:
            data = ser.read(ser.in_waiting or 1)
            if data:
                chunks.append(data)
    finally:
        ser.close()
    return b"".join(chunks).decode("utf-8", "replace")


def flash(elf):
    if not os.path.exists(elf):
        sys.exit(f"elf not found: {elf}")
    print(f"flashing {elf} and resetting target")
    cmd = ["openocd", "-f", "openocd.cfg",
           "-c", f"program {elf} verify reset exit"]
    rc = subprocess.run(cmd).returncode
    if rc != 0:
        sys.exit(f"openocd failed with exit code {rc}")


def parse(text):
    font = []          # list of (lineno, dict) for switch -> 1 (font page)
    boot = None
    boot_lineno = -1
    for lineno, line in enumerate(text.splitlines()):
        b = BOOT_RE.search(line)
        if b:
            boot = b.groupdict()
            boot_lineno = lineno
        m = PAGE_RE.search(line)
        if m and m.group("idx") == "1":
            sd = m.group("ctf_sd")
            font.append((lineno, {
                "refr": int(m.group("refr")),
                "bmp": int(m.group("bmp")),
                "sd": (int(sd) if sd is not None else 0)
                      + int(m.group("ttf_fill") or 0)
                      + int(m.group("ttf_read") or 0),
            }))
    # Only trust font loads that happened during the most recent boot, so a
    # capture that also logged an older firmware (before a reset) does not
    # mis-label a stale warm load as "first".
    font = [s for (ln, s) in font if ln > boot_lineno]
    return font, boot


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="auto",
                    help="COM port or 'auto' (ST-Link VCP); ignored with --in")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=35.0,
                    help="capture window in seconds (need >= 2 font loads)")
    ap.add_argument("--in", dest="infile", default=None,
                    help="parse this capture file instead of going live")
    ap.add_argument("--flash", action="store_true",
                    help="flash the ELF and reset before capturing")
    ap.add_argument("--elf", default="build-release/lvgl_oled.elf",
                    help="ELF to flash when --flash is set")
    ap.add_argument("--tol-us", type=int, default=30000,
                    help="max allowed |first - warm_avg| in microseconds")
    args = ap.parse_args()

    if args.infile:
        with open(args.infile, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        print(f"parsing {args.infile}")
    else:
        if args.flash:
            # Port must be open BEFORE the reset, so pick it, then flash.
            port = select_port(args.port)
            flash(args.elf)
            text = capture(port, args.baud, args.seconds)
        else:
            port = select_port(args.port)
            print(f"auto-selected port: {port}")
            text = capture(port, args.baud, args.seconds)

    font, boot = parse(text)

    print("=" * 60)
    print("Font-page first-load regression")
    print("=" * 60)
    if boot:
        print(f"[BOOT] pending0={boot['p0']} pending={boot['p']} "
              f"elapsed={boot['e']}ms engine={boot['eng']}")
    print(f"font-page samples (switch -> 1): {len(font)}")

    fails = []

    if len(font) < 2:
        fails.append(f"need >= 2 font-page loads to compare first vs warm, got {len(font)}")
        for i, s in enumerate(font):
            print(f"  #{i}: refr={s['refr']}us bmp_miss={s['bmp']} sd={s['sd']}")
        print(f"\n[FAIL] {fails[0]}")
        return 1

    first = font[0]
    warm = font[1:]
    warm_avg = sum(s["refr"] for s in warm) // len(warm)
    delta = first["refr"] - warm_avg

    for i, s in enumerate(font):
        tag = "FIRST" if i == 0 else "warm"
        print(f"  #{i} [{tag}]: refr={s['refr']}us bmp_miss={s['bmp']} sd_reads={s['sd']}")

    print(f"\nfirst refr      : {first['refr']} us")
    print(f"warm  avg refr  : {warm_avg} us  (n={len(warm)})")
    print(f"delta           : {delta} us  (tol +/- {args.tol_us} us)")

    if first["bmp"] != 0:
        fails.append(f"first load had {first['bmp']} glyph-cache misses "
                     f"(Latin preload / warm-up regression)")
    if first["sd"] != 0:
        fails.append(f"first load did {first['sd']} SD reads "
                     f"(glyphs should be cached at first paint)")
    if abs(delta) > args.tol_us:
        fails.append(f"first load {abs(delta)}us off warm avg "
                     f"(outside +/-{args.tol_us}us tolerance)")

    if fails:
        print("\n[FAIL]")
        for f in fails:
            print(f"  - {f}")
        return 1

    print("\n[PASS] first load == warm load: no glyph misses, no SD reads, "
          "first refr within tolerance.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
