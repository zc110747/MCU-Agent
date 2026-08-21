#!/usr/bin/env python3
"""
tools/stress_test.py — Real-hardware shell + stress verification on COM19.

Flow:
  1. Open the serial port (ST-Link VCP, 115200 8N1).
  2. Reset & run the target via OpenOCD.
  3. Wait for the shell prompt, then issue a command sequence:
       help / sys / font / stress cpu / stress kmem / stress lvmem / stress uart
  4. Assert key output substrings; print a pass/fail summary.

Usage: python tools/stress_test.py
"""
import serial
import subprocess
import time
import sys

OPENOCD = r"D:\Software\openocd\bin\openocd.exe"
SCRIPTS = r"D:\Software\openocd\share\openocd\scripts"
PORT = "COM19"
BAUD = 115200
PROMPT = "h743> "
CMD_TIMEOUT = 25  # seconds; stress cpu can take a few seconds

CMDS = [
    ("help",            ["stress"]),
    ("sys",             ["Zephyr", "uptime"]),
    ("font",            ["font mask"]),
    ("stress cpu 2",    ["cpu stress: 2 s", "cpu stress done"]),
    ("stress kmem 200", ["kmem stress: 200 iters", "kmem stress done", "all 16 blocks freed"]),
    ("stress lvmem 200",["lvmem stress: 200 iters", "lvmem stress done", "no leak detected"]),
    ("stress uart 50",  ["uart stress: 50 lines", "uart stress done", "B/s (app-layer)"]),
]


def read_until(ser, marker, timeout_s):
    """Read bytes until marker appears; return decoded text (replace errors)."""
    end = time.time() + timeout_s
    buf = b""
    while time.time() < end:
        data = ser.read(4096)
        if data:
            buf += data
            if marker.encode() in buf:
                break
        else:
            time.sleep(0.05)
    return buf.decode("utf-8", errors="replace")


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.2)
    except Exception as e:
        print(f"SERIAL_OPEN_FAIL: {e!r}")
        return 1

    time.sleep(0.3)
    ser.reset_input_buffer()

    # Reset & run so we start from a clean shell banner.
    cmd = [OPENOCD, "-s", SCRIPTS, "-f", "interface/stlink.cfg",
           "-c", "transport select swd", "-f", "target/stm32h7x.cfg",
           "-c", "adapter speed 4000", "-c", "init",
           "-c", "reset run", "-c", "shutdown"]
    subprocess.run(cmd, capture_output=True, text=True, timeout=40)

    boot = read_until(ser, PROMPT, 12)
    print("=== BOOT ===")
    print(boot[-400:] if len(boot) > 400 else boot)

    total = passed = 0
    for cmd_str, needles in CMDS:
        total += 1
        ser.reset_input_buffer()
        ser.write((cmd_str + "\r").encode())
        out = read_until(ser, PROMPT, CMD_TIMEOUT)
        ok = all(n in out for n in needles)
        passed += 1 if ok else 0
        print(f"\n=== CMD: {cmd_str} -> {'PASS' if ok else 'FAIL'} ===")
        # Trim the echoed command line itself for readability
        body = out.replace(cmd_str + "\r\n", "").replace(cmd_str + "\n", "")
        print(body.rstrip())

    ser.close()
    print(f"\nRESULT: {passed}/{total} passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
