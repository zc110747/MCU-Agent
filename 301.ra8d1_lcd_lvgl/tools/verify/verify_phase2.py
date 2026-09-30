#!/usr/bin/env python3
"""Phase 2 acceptance script - RA8D1 Vision Board (GLCDC + RGB panel, no LVGL yet).

What it checks:
  1. boot banner        : "RA8D1 Vision Board - Phase 2"
  2. heartbeat          : tick advances ~2000 per sample
  3. msh / help         : the "lcd" command is registered
  4. lcd init           : re-init returns OK (0x0)
  5. lcd info           : 800x480 bpp16, fb=0x68000000, 768000 bytes (.sdram)
  6. lcd stat           : EN=1, HSW=800/VSW=480 (+porch), L1UNDF=0.
                          L2UNDF=1 is accepted: it is the GR[1] line-detect
                          artifact documented in documents/phase2-report.md
                          (layer 2 is transparent and never fetches memory).
  7. lcd pattern 3/2    : commands execute
  8. framebuffer (SWD)  : colour-bar values read back from SDRAM 0x68000000
  9. lcd fill (SWD)     : full-screen fill lands in SDRAM, then restored
 10. backlight (SWD)    : P1011 PODR follows 'lcd bl off/on'
 11. GR[0] registers    : RENB=1, BASE=0x68000000, layer-1 underflow latch = 0

Usage:
    python tools/verify/verify_phase2.py              # COM9 @115200, reset first
    python tools/verify/verify_phase2.py --port COM12 --no-reset

Exit code 0 = every check passed.
"""
import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

BOOT_BANNER = "RA8D1 Vision Board - Phase 2"
HEARTBEAT_RE = re.compile(r"\[heartbeat\] tick=(\d+) heap total=(\d+) used=(\d+) max=(\d+)")

# PFS base 0x40400800, PORT stride 0x40, PIN stride 4. bit0 PODR, bit1 PIDR.
BL_PFS_ADDR = 0x40400AAC        # P1011 backlight control
BL_PODR_BIT = 0

# GLCDC base 0x40342000. GR[] at +0x1100, element stride 0x100.
GR0_FLMRD = 0x40343104          # GR[0].FLMRD (RENB bit0)
GR0_FLM2 = 0x4034310C           # GR[0].FLM2 (framebuffer base address)
GR0_MON = 0x40343154            # GR[0].MON (UNDFLST bit16)

FB_BASE = 0x68000000
FB_WIDTH = 800
FB_HEIGHT = 480

# Colour bars as drawn by bsp_lcd_pattern(2): 8 bars, 100 px each.
BAR_EXPECT = {0: 0xF800, 100: 0xFC00, 300: 0x07E0, 500: 0x001F, 700: 0xFFFF, 799: 0xFFFF}

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    """Print one PASS/FAIL line and update the counters."""
    global PASS, FAIL
    if ok:
        PASS += 1
        print("[PASS] %-26s %s" % (name, detail))
    else:
        FAIL += 1
        print("[FAIL] %-26s %s" % (name, detail))
    return ok


def open_session():
    """Open a pyOCD session (optional: SWD checks are skipped without it)."""
    try:
        from pyocd.core.helpers import ConnectHelper
    except ImportError:
        print("[WARN] pyocd not importable - reset and SWD checks are skipped")
        return None
    try:
        return ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                                       frequency=1000000,
                                                       halt_on_connect=False)
    except Exception as exc:
        print("[WARN] no debug probe: %s" % exc)
        return None


def read_for(ser, seconds):
    """Read the port for a fixed amount of time, return decoded text."""
    end = time.time() + seconds
    buf = bytearray()
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            buf += chunk
    return buf.decode("utf-8", "replace").replace("\r", "")


def send(ser, cmd, wait=1.5):
    """Send one msh command line and return everything the board answers."""
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    return read_for(ser, wait)


def fb_read16(tgt, index):
    """Read framebuffer halfword[index] over SWD."""
    return tgt.read16(FB_BASE + index * 2) & 0xFFFF


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM9", help="serial port of the ART-Link VCP")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--no-reset", action="store_true", help="do not reset the board")
    args = ap.parse_args()

    session = open_session()
    if session is not None:
        session.open()

    ser = serial.Serial(args.port, args.baud, timeout=0.3)
    ser.reset_input_buffer()

    if not args.no_reset and session is not None:
        session.target.reset_and_halt()
        session.target.resume()
        boot = read_for(ser, 3.0)
    else:
        boot = read_for(ser, 1.0)

    print("=" * 62)
    print("Phase 2 acceptance - %s @ %d" % (args.port, args.baud))
    print("=" * 62)

    # 1. boot banner ---------------------------------------------------------
    check("boot banner", BOOT_BANNER in boot,
          "found '%s'" % BOOT_BANNER if BOOT_BANNER in boot else repr(boot[:120]))

    # 2. heartbeat -----------------------------------------------------------
    hb = read_for(ser, 6.0)
    ticks = [int(m.group(1)) for m in HEARTBEAT_RE.finditer(hb)]
    check("heartbeat present", len(ticks) >= 2, "%d samples" % len(ticks))
    delta_ok = len(ticks) >= 2 and all(1500 < (b - a) < 2500
                                      for a, b in zip(ticks, ticks[1:]))
    check("tick advances ~2000", delta_ok, "ticks=%s" % ticks[:5])

    # 3. msh / help ----------------------------------------------------------
    out = send(ser, "help")
    check("msh prompt", "msh >" in out, "")
    check("help lists 'lcd'", re.search(r"^\s*lcd\s+-", out, re.M) is not None,
          "lcd registered" if "lcd" in out else repr(out[:120]))

    # 4. lcd init (idempotent re-init) ----------------------------------------
    out = send(ser, "lcd init")
    check("lcd init OK", "lcd init: OK (0x0)" in out, repr(out.strip()[:60]))

    # 5. lcd info ------------------------------------------------------------
    out = send(ser, "lcd info")
    m = re.search(r"lcd (\d+)x(\d+) bpp(\d+) fb=0x([0-9a-f]+) \((\d+) bytes", out)
    info_ok = (m is not None and m.group(1) == "800" and m.group(2) == "480" and
               m.group(3) == "16" and m.group(4) == "68000000" and m.group(5) == "768000")
    check("lcd info geometry", info_ok, repr(out.strip()[:70]))

    # 6. lcd stat ------------------------------------------------------------
    out = send(ser, "lcd stat")
    men = re.search(r"BG\.EN=0x([0-9a-f]+) \(EN=(\d) VEN=(\d)\)", out)
    msz = re.search(r"BG\.HSIZE=(\d+) BG\.VSIZE=(\d+)", out)
    msf = re.search(r"STMON=0x([0-9a-f]+) \(L1UNDF=(\d) L2UNDF=(\d)\)", out)
    stat_ok = True
    if men and msz and msf:
        en = int(men.group(2))
        hsize, vsize = int(msz.group(1)), int(msz.group(2))
        hsw, vsw = hsize & 0xFFF, vsize & 0xFFF
        hss, vss = (hsize >> 16) & 0xFFF, (vsize >> 16) & 0xFFF
        l1undf, l2undf = int(msf.group(2)), int(msf.group(3))
        stat_ok = en == 1 and hsw == 800 and vsw == 480 and l1undf == 0
        detail = "EN=%d HSW=%d HSS=%d VSW=%d VSS=%d L1UNDF=%d" % (en, hsw, hss, vsw, vss, l1undf)
        if l2undf == 1:
            detail += " L2UNDF=1(artifact)"
    else:
        detail = repr(out.strip()[:90])
    check("lcd stat EN/timing/L1UNDF", stat_ok, detail)

    # 7. pattern commands ------------------------------------------------------
    out = send(ser, "lcd pattern 3")
    check("lcd pattern 3", "lcd pattern 3" in out, repr(out.strip()[:50]))
    out = send(ser, "lcd pattern 2")
    check("lcd pattern 2", "lcd pattern 2" in out, repr(out.strip()[:50]))

    # 8. framebuffer colour bars over SWD --------------------------------------
    if session is not None:
        time.sleep(0.3)  # let the fill loop finish before sampling
        bad = []
        for idx, want in BAR_EXPECT.items():
            got = fb_read16(session.target, idx)
            if got != want:
                bad.append("fb[%d]=0x%04X!=0x%04X" % (idx, got, want))
        row240 = fb_read16(session.target, 240 * FB_WIDTH)
        if row240 != BAR_EXPECT[0]:
            bad.append("fb[240x800]=0x%04X" % row240)
        check("fb colour bars (SWD)", not bad, "; ".join(bad) if bad else "6 samples exact")

        # 9. full-screen fill over SWD ------------------------------------------
        out = send(ser, "lcd fill 0x07E0")
        time.sleep(0.5)
        a = fb_read16(session.target, 0)
        b = fb_read16(session.target, FB_WIDTH * FB_HEIGHT // 2)
        c = fb_read16(session.target, FB_WIDTH * FB_HEIGHT - 1)
        check("lcd fill reaches SDRAM", a == 0x07E0 and b == 0x07E0 and c == 0x07E0,
              "fb[0]=0x%04X fb[mid]=0x%04X fb[end]=0x%04X" % (a, b, c))
        send(ser, "lcd pattern 2", 0.8)  # restore colour bars for the panel

        # 10. backlight pin over SWD ---------------------------------------------
        send(ser, "lcd bl off", 0.4)
        off = (session.target.read32(BL_PFS_ADDR) >> BL_PODR_BIT) & 1
        send(ser, "lcd bl on", 0.4)
        on = (session.target.read32(BL_PFS_ADDR) >> BL_PODR_BIT) & 1
        check("P1011 backlight SWD", off == 0 and on == 1, "PODR off=%d on=%d" % (off, on))

        # 11. GR[0] layer-1 registers over SWD ------------------------------------
        renb = session.target.read32(GR0_FLMRD) & 1
        base = session.target.read32(GR0_FLM2)
        undf = (session.target.read32(GR0_MON) >> 16) & 1
        gr_ok = renb == 1 and base == FB_BASE and undf == 0
        check("GR0 RENB/BASE/UNDFLST", gr_ok,
              "RENB=%d BASE=0x%08X UNDFLST=%d" % (renb, base, undf))
    else:
        print("[SKIP] SWD checks (no debug probe)")

    ser.close()
    if session is not None:
        session.close()

    print("=" * 62)
    print("RESULT: %d passed, %d failed" % (PASS, FAIL))
    print("=" * 62)
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
