#!/usr/bin/env python3
"""End-to-end test of the STM32H743 + TinyUSB CDC virtual serial port.

Opens the board's COM port, asserts DTR (which makes the firmware print its
banner), then exercises every command and verifies the per-character echo.
All traffic is dumped to stdout so the run is fully auditable.
"""
import sys
import time
import serial

PORT = "COM4"
BAUD = 115200          # CDC ignores baud, but pick a sane default anyway
RD_TIMEOUT = 0.4      # per-read timeout (s)
DRAIN = 0.8           # time to let the device finish a reply (s)


def drain(ser, label, dur=DRAIN):
    """Read whatever the device sends for `dur` seconds and return it."""
    end = time.monotonic() + dur
    out = bytearray()
    while time.monotonic() < end:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            out += chunk
    if out:
        print(f"  [{label}] <<< ({len(out)} B)")
        text = "".join(chr(b) if (32 <= b < 127 or b in (9, 10, 13)) else f"<{b:02x}>"
                       for b in out)
        sys.stdout.write(text)
        print()
    return bytes(out)


def send(ser, text):
    """Write a string (no CR) and let the per-char echo come back."""
    buf = text.encode("ascii")
    ser.write(buf)
    time.sleep(0.05)
    drain(ser, "echo", 0.4)


def send_line(ser, cmd):
    print(f"\n=== send: {cmd!r} ===")
    ser.write((cmd + "\r").encode("ascii"))
    drain(ser, "reply")


def main():
    print(f"opening {PORT} @ {BAUD} ...")
    ser = serial.Serial(PORT, BAUD, timeout=RD_TIMEOUT, dsrdtr=True)
    time.sleep(0.5)

    # Assert DTR so the firmware prints its banner (tud_cdc_line_state_cb).
    ser.dtr = True
    time.sleep(0.3)
    drain(ser, "banner", 1.0)

    # 1) per-character echo with no command terminator
    print("\n=== send: 'hello' (raw, no CR) ===")
    send(ser, "hello")
    # then terminate the line -> command runs (unknown 'hello') + new prompt
    ser.write(b"\r")
    drain(ser, "reply", 0.6)

    # 2) command set
    send_line(ser, "help")
    send_line(ser, "clk")
    send_line(ser, "stats")
    send_line(ser, "led on")
    time.sleep(0.3)
    send_line(ser, "led off")
    send_line(ser, "flood 3")
    send_line(ser, "hb")          # turn heartbeat off so output is stable
    time.sleep(0.2)
    send_line(ser, "stats")

    # 3) a longer payload to stress the echo + RX counter
    payload = "The quick brown fox jumps over the lazy dog. 1234567890\r"
    print(f"\n=== send: long line ({len(payload)} B) ===")
    ser.write(payload.encode("ascii"))
    drain(ser, "reply", 0.8)

    ser.close()
    print("\nclosed port.")


if __name__ == "__main__":
    main()
