#!/usr/bin/env python3
"""Capture a COM port to a file for a fixed amount of time.

Used to grab the boot banner: the firmware only prints it once, so the port has
to be open *before* the target is reset.

    python scripts/serial_capture.py --port COM19 --seconds 25 --out out.txt

Run it in the background, then flash / reset the board with OpenOCD.
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required:  pip install pyserial")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="e.g. COM19")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=20.0,
                    help="how long to keep the port open")
    ap.add_argument("--out", default="serial_capture.txt")
    args = ap.parse_args()

    chunks = []
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as exc:
        print(f"cannot open {args.port}: {exc}", file=sys.stderr)
        return 1

    print(f"capturing {args.port} @ {args.baud} for {args.seconds}s -> {args.out}")
    end = time.time() + args.seconds
    try:
        while time.time() < end:
            data = ser.read(ser.in_waiting or 1)
            if data:
                chunks.append(data)
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
    finally:
        ser.close()

    with open(args.out, "wb") as fh:
        fh.write(b"".join(chunks))
    print(f"\n{sum(len(c) for c in chunks)} bytes written to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
