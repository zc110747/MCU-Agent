#!/usr/bin/env python3
"""Interactive-ish serial console capture for the RA8D1 firmware.

Opens COMx at 115200 8N1, captures boot output, then sends a list of
commands (one per line) with a settle delay between each, dumping all
received bytes.  Used to diagnose the CST812T touch path live.
"""
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not available")

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM9"
BAUD = 115200

# Remaining argv after port are commands to send (space separated or as args).
# Simpler: read commands from stdin lines if piped, else use defaults.
DEFAULT_CMDS = [
    "touch init",
    "touch scan",
    "touch read",
    "touch id",
]

cmds = DEFAULT_CMDS
if len(sys.argv) > 2:
    cmds = sys.argv[2:]

ser = serial.Serial(PORT, BAUD, timeout=0.2)
time.sleep(0.3)
ser.reset_input_buffer()

def drain(label, secs):
    end = time.time() + secs
    buf = []
    while time.time() < end:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            try:
                s = chunk.decode("utf-8", "replace")
            except Exception:
                s = repr(chunk)
            buf.append(s)
            sys.stdout.write(s)
            sys.stdout.flush()
    print(f"\n--- [{label}] captured {len(''.join(buf))} chars ---")

print(f"=== console on {PORT} {BAUD} ===")
# Boot log
drain("boot", 6.0)

for c in cmds:
    ser.write((c + "\n").encode("utf-8"))
    print(f"\n>>> sent: {c}")
    drain(f"resp:{c}", 4.0)

ser.close()
print("\n=== done ===")
