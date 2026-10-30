"""Dump a COM port to a log file for a fixed number of seconds.

Usage:  python tools/serial_log.py COM3 build/serial.log [seconds]

The board prints at 115200 8N1.  Lines are flushed as they arrive so the file is
usable even when the process is killed with a timeout.
"""
import sys
import time

import serial

BAUD = 115200


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    port = sys.argv[1]
    path = sys.argv[2]
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 15.0

    link = serial.Serial(port, BAUD, timeout=0.2)
    deadline = time.time() + seconds
    with open(path, "wb") as out:
        while time.time() < deadline:
            chunk = link.read(4096)
            if chunk:
                out.write(chunk)
                out.flush()
    link.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
