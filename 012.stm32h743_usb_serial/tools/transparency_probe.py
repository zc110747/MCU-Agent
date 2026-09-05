#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# Transparency probe for the H743 USB<->UART4 bridge.
#
# Purpose: prove two things after the in-band AT / Hayes-escape mechanism was
# removed and the DTR gate was taken out of the data path:
#
#   1. DTR OFF still forwards data  (DTR is a modem-status line, not a gate).
#   2. Arbitrary bytes - including the old "+++" escape and "AT+FLOW=1" - are
#      forwarded verbatim and are NEVER consumed as a command.
#
# Everything is driven only through the stock serial API (baud / dtr / rts) -
# exactly what a normal host terminal program or pyserial app would do. No
# private control channel is used, mirroring the firmware design.
# ---------------------------------------------------------------------------
import sys
import time

sys.path.insert(0, "tools")
import stress_test as S          # reuse find_ports / open_serial helpers
import serial


def find_port():
    matched, _all = S.find_ports()
    if not matched:
        raise SystemExit("No project port (VID:PID=%04X:%04X) found" %
                         (S.PROJECT_VID, S.PROJECT_PID))
    if len(matched) > 1:
        for p in matched:
            print("  ", p.device, p.hwid)
        raise SystemExit("Several matching ports - pass --port")
    return matched[0].device


def loopback_echo(ser, payload, timeout=4.0):
    """Send `payload`, read back exactly len(payload) bytes, return them."""
    ser.reset_input_buffer()
    ser.write(payload)
    ser.flush()
    got = b""
    deadline = time.time() + timeout
    while len(got) < len(payload) and time.time() < deadline:
        n = ser.in_waiting
        chunk = ser.read(n if n else 1)
        if chunk:
            got += chunk
    return got


def run_case(port, dtr, label):
    ser = S.open_serial(port, 115200, timeout=0.1, write_timeout=5.0)
    try:
        # pyserial asserts DTR and RTS on open; override as requested.
        ser.dtr = dtr
        ser.rts = True                 # RTS asserted -> firmware flow control OFF
        time.sleep(0.2)

        # A string that, on the OLD firmware, would have dropped into command
        # mode and been swallowed instead of forwarded.
        payload = (b"+++AT+FLOW=1\rAT+LOOP=0\r"
                   b"\x00\x01\x02\xfe\xff"
                   b"The quick brown fox jumps over the lazy dog.\r\n")
        echo = loopback_echo(ser, payload)

        ok = (echo == payload)
        # Show whether the dangerous bytes survived.
        esc_ok = (b"+++AT+FLOW=1" in echo) and (b"AT+LOOP=0" in echo)
        print(f"  [{label}] DTR={'ON ' if dtr else 'OFF'} "
              f"echo_len={len(echo)}/{len(payload)} "
              f"verbatim={ok} escape_survived={esc_ok} -> "
              f"{'PASS' if ok else 'FAIL'}")
        return ok and esc_ok
    finally:
        ser.close()


def main():
    port = find_port()
    print(f"Project port: {port}")
    print("Assumes PA0<->PA1 are hardware-shorted (UART loopback).")
    print("Proving the bridge is transparent and DTR-independent:\n")

    a = run_case(port, True,  "DTR on ")
    b = run_case(port, False, "DTR off")

    print()
    if a and b:
        print("ALL PASS - data is forwarded verbatim with DTR on or off; "
              "no in-band command is ever intercepted.")
        raise SystemExit(0)
    else:
        print("FAIL - see above.")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
