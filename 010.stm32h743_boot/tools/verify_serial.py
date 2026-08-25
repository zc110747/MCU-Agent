# ============================================================================
# tools/verify_serial.py
# Capture the ST-Link VCP (USART1) output and run pass/fail acceptance checks
# for the bootloader + app. Intended to be run after a reset of the board.
#
# Expected scenarios (pick with --expect):
#   jump     : bootloader validates config and jumps to the app (default)
#   udisk    : no valid app / USB connected -> stays in U-disk mode
#   upgrade  : an upgrade package on the QSPI U-disk is processed
#
# Usage:
#   python tools/verify_serial.py --port COM19 --expect jump
# ============================================================================
import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("ERR: pyserial not installed (pip install pyserial)")
    sys.exit(2)

PASS, FAIL = 0, 0


def check(cond, msg):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("  PASS  %s" % msg)
    else:
        FAIL += 1
        print("  FAIL  %s" % msg)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM19")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=15.0)
    ap.add_argument("--expect", choices=["jump", "udisk", "upgrade"], default="jump")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1.0)
    except Exception as e:
        print("ERR: cannot open %s: %s" % (args.port, e))
        return 2

    print("capturing %s for %.0f s ..." % (args.port, args.timeout))
    buf = []
    t0 = time.time()
    while (time.time() - t0) < args.timeout:
        line = ser.readline().decode("utf-8", "ignore")
        if line:
            sys.stdout.write(line)
            buf.append(line)
        if args.expect == "jump" and "app alive @1Hz" in line:
            break
        if args.expect == "upgrade" and "upgrade SUCCESS" in line:
            break
    ser.close()
    text = "".join(buf)

    print("\n== acceptance checks (%s) ==" % args.expect)
    if args.expect == "jump":
        check("[BOOT] app image OK" in text, "bootloader validated app image")
        check("jumping to app" in text, "bootloader jumped to app")
        check("TEST APP" in text or "app alive @1Hz" in text, "app is running (1 Hz heartbeat)")
    elif args.expect == "upgrade":
        check("HMAC-SHA256 verified OK" in text, "package HMAC verified")
        check("upgrade SUCCESS" in text, "upgrade completed")
    elif args.expect == "udisk":
        check("U-disk mode" in text, "bootloader stayed in U-disk mode")

    print("\nRESULT: %d passed, %d failed" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
