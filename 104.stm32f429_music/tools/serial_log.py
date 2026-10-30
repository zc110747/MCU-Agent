"""Dump a COM port to a log file for a fixed number of seconds.

Usage:  python tools/serial_log.py COM3 build/serial.log [seconds] [stamp]

The board prints at 115200 8N1.  Lines are flushed as they arrive so the file is
usable even when the process is killed with a timeout.

Pass "stamp" as the fourth argument to prefix every line with the arrival time in
seconds since the capture started -- that is how the hand-over timings are
measured, no firmware instrumentation needed.
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
    stamp = (len(sys.argv) > 4) and (sys.argv[4].lower() == "stamp")

    link = serial.Serial(port, BAUD, timeout=0.2)
    start = time.time()
    deadline = start + seconds
    pending = ""

    with open(path, "wb") as out:
        while time.time() < deadline:
            chunk = link.read(4096)
            if not chunk:
                continue
            if not stamp:
                out.write(chunk)
                out.flush()
                continue

            pending += chunk.decode("utf-8", "replace")
            while "\n" in pending:
                line, pending = pending.split("\n", 1)
                now = time.time()
                stamp_text = time.strftime("%H:%M:%S", time.localtime(now))
                text = "%s.%03d  %s\n" % (stamp_text, int((now % 1) * 1000), line.rstrip("\r"))
                out.write(text.encode("utf-8"))
            out.flush()

    link.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
