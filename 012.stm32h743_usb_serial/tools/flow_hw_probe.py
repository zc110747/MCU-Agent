#!/usr/bin/env python3
"""Hardware RTS/CTS flow control evidence for the STM32H743 USB<->UART4 bridge.

Board wiring for this test: PB0 (CTS) is shorted to PB14 (RTS) so the bridge's
own RTS output controls its own CTS input. With hardware CTS always enabled at
the USART (CTSE), the test proves the TX path is gated in SILICON:

  host RTS asserted  -> PB14 low  -> CTS low  -> USART TX flows
  host RTS deasserted-> PB14 high -> CTS high -> USART TX HALTED (hardware)

This is the standard, transparent mapping requested: RTS is passed straight
through to the UART's RTS pin; DTR has no physical pin and is observed only;
CTS (TX gating) is peer-controlled and always on.

Usage:
    python tools/flow_hw_probe.py [--port COM15]

------------------------------------------------------------------------------
IMPORTANT (Windows host driver limitation - NOT a firmware bug)
------------------------------------------------------------------------------
On Windows the stock usbser.sys CDC-ACM driver does NOT forward an RTS state
change to the device once the port is open. Only DTR changes reach the firmware
(CLRDTR / SETDTR trigger tud_cdc_line_state_cb; CLRRTS / SETRTS do NOT). This
was confirmed by experiment: writing a 0xFD marker from the firmware on every
line-state callback showed markers for DTR toggles but never for RTS toggles.

Consequence:
  * Test [4] (DTR-only) works on Windows  -> data still flows. Good.
  * Tests [2]/[3] (deassert/re-assert RTS to gate TX) will NOT gate on Windows,
    because the firmware never sees the RTS change. They PASS only on a host
    whose CDC driver forwards RTS - e.g. Linux / macOS, or a Windows composite
    driver that exposes RTS. This is expected and is a host-driver property,
    not a defect in the bridge.

The bridge itself is correct on every host:
  * hardware CTS gating (TX paused by the peer's CTS in silicon) is ALWAYS on;
  * our RTS output is driven in firmware from the host RTS signal ANDed with the
    RX-ring headroom, protecting the 16 KB ring.
On a real peer (not a PA0<->PA1 loopback) flow control needs no host switch at
all - the peer simply drives CTS when it cannot accept more bytes.
"""

import sys
import time
import serial
import serial.tools.list_ports

PROJECT_VID = "CAFE"
PROJECT_PID = "4012"

# Single marker used both for the gated probe and for the resume check, so the
# two halves of the test can never disagree on what "the gated bytes" are.
GATED_MARKER = b"GATEDHALT_MARKER_ABCDEFGHIJKLMNOP_1234567890\r\n"


def find_project_port():
    for p in serial.tools.list_ports.comports():
        hwid = (p.hwid or "").upper()
        if PROJECT_VID in hwid or PROJECT_PID in hwid:
            return p.device
    return None


def open_serial(port, baud=115200):
    for _ in range(20):
        try:
            s = serial.Serial(port, baud, timeout=0.4, write_timeout=2.0)
            return s
        except Exception:
            time.sleep(0.15)
    raise SystemExit("cannot open " + port)


def drain(ser, t=0.15):
    time.sleep(t)
    while ser.in_waiting:
        ser.read(ser.in_waiting)


def read_echo(ser, n, timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end and len(buf) < n:
        k = ser.in_waiting
        if k:
            buf += ser.read(k)
        else:
            time.sleep(0.005)
    return buf


def send_and_check(ser, label, payload, expected, timeout=0.5):
    drain(ser)
    ser.write(payload)
    got = read_echo(ser, len(expected), timeout)
    ok = (got == expected)
    print(f"  [{label}] tx={len(payload)}B rx={len(got)}B "
          f"{'PASS' if ok else 'FAIL'}  {repr(got[:32]) if not ok else ''}")
    return ok


def main():
    port = sys.argv[sys.argv.index("--port") + 1] if "--port" in sys.argv else find_project_port()
    if not port:
        raise SystemExit("project COM port not found (VID:PID %s:%s)" % (PROJECT_VID, PROJECT_PID))

    print(f"Project port: {port}")
    print("Assumes PB0 (CTS) is shorted to PB14 (RTS) on the board.\n")

    all_ok = True

    # ---- 1) baseline: RTS+DTR asserted (pyserial default) -> TX flows ----
    print("[1] RTS+DTR asserted (default) -> transparent pass-through")
    ser = open_serial(port)
    ser.rts = True
    ser.dtr = True
    time.sleep(0.3)
    all_ok &= send_and_check(ser, "echo", b"HW_FLOW_BASELINE_OK\r\n",
                              b"HW_FLOW_BASELINE_OK\r\n")
    all_ok &= send_and_check(ser, "echo2", b"RTS_ASSERTED_TX_FLOWS\r\n",
                              b"RTS_ASSERTED_TX_FLOWS\r\n")

    # ---- 2) HARDWARE gate: deassert host RTS -> PB14 high -> CTS high -> TX HALT ----
    print("\n[2] deassert host RTS -> USART hardware halts TX (CTS high)")
    ser.rts = False                 # firmware releases PB14 -> CTS (PB0) high
    time.sleep(0.6)                 # let uart_flow_service drop RTS (past a loop)
    drain(ser)
    ser.write(GATED_MARKER)         # should NOT loop back while CTS is high
    got = read_echo(ser, len(GATED_MARKER), timeout=1.2)
    halted = (len(got) == 0)
    if halted:
        print(f"  [gated]  tx={len(GATED_MARKER)}B rx={len(got)}B  PASS  "
              f"(TX halted by hardware CTS)")
    else:
        # On Windows usbser the RTS change never reached the device, so the
        # bytes came straight back. That is a host-driver limitation, not a
        # firmware fault - flag it clearly instead of failing silently.
        print(f"  [gated]  tx={len(GATED_MARKER)}B rx={len(got)}B  "
              f"NOT GATED (RTS not forwarded by this host driver)")
        print("          -> expected on Windows usbser.sys; run on Linux/macOS "
              "or a CDC driver that forwards RTS to verify silicon gating.")
    all_ok &= halted

    # ---- 3) resume: re-assert host RTS -> TX resumes, stuck bytes flush out ----
    print("\n[3] re-assert host RTS -> TX resumes (stuck bytes flush out)")
    ser.rts = True                  # PB14 low -> CTS low -> USART TX resumes
    time.sleep(0.2)
    # The bytes sent while gated are still in the TX ring; they flush now.
    got = read_echo(ser, len(GATED_MARKER), timeout=0.6)
    flushed = (len(got) >= len(GATED_MARKER)) and (GATED_MARKER in got)
    print(f"  [resume] rx={len(got)}B  "
          f"{'PASS' if flushed else 'FAIL'}  (gated bytes recovered: {flushed})")
    all_ok &= flushed
    all_ok &= send_and_check(ser, "echo3", b"RTS_REASSERTED_TX_RESUMES\r\n",
                              b"RTS_REASSERTED_TX_RESUMES\r\n")

    # ---- 4) DTR alone must NOT gate the data path (observed only) ----
    print("\n[4] DTR deasserted only (RTS stays asserted) -> data still flows")
    ser.rts = True
    ser.dtr = False
    time.sleep(0.2)
    all_ok &= send_and_check(ser, "echo_dtr_off", b"DTR_OFF_STILL_WORKS\r\n",
                              b"DTR_OFF_STILL_WORKS\r\n")
    ser.dtr = True

    ser.close()
    print("\n" + ("ALL PASS - hardware RTS/CTS flow control verified."
                  if all_ok else "SOME CHECKS FAILED (see notes above)"))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
