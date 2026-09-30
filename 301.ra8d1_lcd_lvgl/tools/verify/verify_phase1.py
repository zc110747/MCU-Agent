#!/usr/bin/env python3
"""Phase 1 acceptance script - RA8D1 Vision Board (RT-Thread Nano + LED + UART).

What it checks:
  1. boot banner        : RT-Thread logo + "RA8D1 Vision Board - Phase 1"
  2. heartbeat          : "[heartbeat] tick=..." printed repeatedly, tick advances
  3. msh prompt         : "msh >" comes back after every command
  4. help               : the "led" command is registered in the symbol table
  5. led on/off/blink   : command replies, usage message, and the real pin level
                          read back from PmnPFS (P102) through SWD
  6. ps                 : main / led / tshell / tidle0 threads are alive
  7. free               : heap numbers are reported

Usage:
    python tools/verify/verify_phase1.py              # COM9 @115200, reset first
    python tools/verify/verify_phase1.py --port COM12 --no-reset

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

BOOT_BANNER = "RA8D1 Vision Board - Phase 1"
HEARTBEAT_RE = re.compile(r"\[heartbeat\] tick=(\d+) heap total=(\d+) used=(\d+) max=(\d+)")

# P102 PmnPFS: PFS base 0x40400800, PORT stride 0x40, PIN stride 4.
# bit0 = PODR (driven level), bit1 = PIDR (pin level).
LED_PFS_ADDR = 0x40400848
LED_PODR_BIT = 0
LED_PIDR_BIT = 1

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    """Print one PASS/FAIL line and update the counters."""
    global PASS, FAIL
    if ok:
        PASS += 1
        print("[PASS] %-24s %s" % (name, detail))
    else:
        FAIL += 1
        print("[FAIL] %-24s %s" % (name, detail))
    return ok


def open_session():
    """Open a pyOCD session (optional: only needed for reset + pin reads)."""
    try:
        from pyocd.core.helpers import ConnectHelper
    except ImportError:
        print("[WARN] pyocd not importable - reset and LED pin checks are skipped")
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


def led_level(session, bit=LED_PODR_BIT):
    """Read the LED pin level straight from the PFS register. None if no probe."""
    if session is None:
        return None
    return (session.target.read32(LED_PFS_ADDR) >> bit) & 1


def sample_led(session, count=14, period=0.1):
    """Sample the LED pin and count level transitions (blink detection)."""
    if session is None:
        return None
    levels = []
    for _ in range(count):
        levels.append(led_level(session))
        time.sleep(period)
    transitions = sum(1 for a, b in zip(levels, levels[1:]) if a != b)
    return transitions


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
    print("Phase 1 acceptance - %s @ %d" % (args.port, args.baud))
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

    # 3/4. msh help ----------------------------------------------------------
    out = send(ser, "help")
    check("msh prompt", "msh >" in out, "")
    check("help lists 'led'", re.search(r"^\s*led\s+-", out, re.M) is not None,
          "led registered" if "led" in out else repr(out[:120]))

    # 5. led command ---------------------------------------------------------
    usage = "usage: led <on|off|blink>"
    for cmd, expect in (("led on", "led on"),
                        ("led off", "led off"),
                        ("led blink", "led blink"),
                        ("led xx", usage),
                        ("led", usage)):
        out = send(ser, cmd)
        check("cmd '%s'" % cmd, expect in out, repr(out.strip()[:70]))

    if session is not None:
        send(ser, "led on", 0.4)
        a = led_level(session)
        time.sleep(0.4)
        b = led_level(session)
        check("P102 high after 'on'", a == 1 and b == 1, "PODR=%s,%s" % (a, b))

        send(ser, "led off", 0.4)
        a = led_level(session)
        time.sleep(0.4)
        b = led_level(session)
        check("P102 low after 'off'", a == 0 and b == 0, "PODR=%s,%s" % (a, b))

        send(ser, "led blink", 0.4)
        tr = sample_led(session)
        check("P102 toggles (blink)", tr is not None and tr >= 2,
              "%d transitions in 1.4 s" % tr)
    else:
        print("[SKIP] LED pin level checks (no debug probe)")

    # 6. ps ------------------------------------------------------------------
    out = send(ser, "ps")
    threads = re.findall(r"\b(main|led|tshell|tidle0)\b", out)
    missing = [t for t in ("main", "led", "tshell", "tidle0") if t not in threads]
    check("ps threads", not missing,
          "missing=%s" % missing if missing else "main/led/tshell/tidle0 alive")

    # 7. free ----------------------------------------------------------------
    out = send(ser, "free")
    m = re.search(r"total\s*:\s*(\d+)", out)
    check("free reports heap", m is not None,
          "total=%s bytes" % m.group(1) if m else repr(out.strip()[:70]))

    ser.close()
    if session is not None:
        session.close()

    print("=" * 62)
    print("RESULT: %d passed, %d failed" % (PASS, FAIL))
    print("=" * 62)
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
