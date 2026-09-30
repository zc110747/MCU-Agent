"""Read the board's console for a fixed time, from a real reset.

Why this exists
---------------
`idf.py monitor` is interactive: it takes over the terminal and only exits on
Ctrl+].  That is useless when the caller is an agent whose stdout is swallowed.
This script does the two things the monitor does that actually matter -- reset
the chip so the boot log is not missed, then dump the port -- and then exits on
its own with a normal status code.

Reset sequence
--------------
The board is on its USB-Serial-JTAG peripheral (VID 303A / PID 1001), where the
control lines do NOT mean what they mean on a UART bridge.  Two esptool
sequences exist and picking the wrong one is a 30-second detour:

  USBJTAGSerialReset   DTR low -> "Set IO0", then an RTS pulse.  This raises
                       download mode: the ROM answers "waiting for download"
                       and the application never runs.  That is what you want
                       when flashing, not when reading a boot log.
  HardReset(usb)       RTS high 200 ms, then low.  DTR stays HIGH, i.e. IO0
                       stays HIGH, so the chip resets straight into the app.

This script uses the second one.  _set_rts() keeps esptool's Windows
work-around: usbser.sys only pushes a control-line-state request when DTR
changes, so an RTS update has to be smuggled through a same-value DTR write.

Usage
-----
    python tools/serial_probe.py COM14 [seconds] [--no-reset]
"""

import sys
import time
import pathlib

import serial

IDLE_SETTLE = 0.10
DEFAULT_SECONDS = 15.0


def _set_rts(ser, state):
    ser.setRTS(state)
    # Windows usbser.sys work-around: re-write DTR with its current value so the
    # control-line-state request is actually sent with the new RTS state.
    ser.setDTR(ser.dtr)


def reset_chip(ser):
    """Hard reset into the application (IO0/BOOT stays HIGH).

    Do NOT copy esptool's USBJTAGSerialReset here: that one drives DTR low,
    which this peripheral reads as "hold IO0 low", and the board comes up in
    download mode instead of running the firmware.
    """
    ser.setDTR(False)         # IO0 = HIGH -> normal boot, not download mode
    _set_rts(ser, True)       # EN -> LOW, chip held in reset
    time.sleep(0.2)
    _set_rts(ser, False)      # release; the chip boots the application
    time.sleep(0.2)


def main(argv):
    if len(argv) < 2:
        print("usage: serial_probe.py <port> [seconds] [--no-reset]")
        return 2

    port = argv[1]
    seconds = DEFAULT_SECONDS
    do_reset = True
    for arg in argv[2:]:
        if arg == "--no-reset":
            do_reset = False
        else:
            seconds = float(arg)

    # The console is USB CDC: the host baud rate is irrelevant, but pyserial
    # still wants one.
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()

    if do_reset:
        try:
            reset_chip(ser)
        except OSError as exc:
            # pyserial on some drivers refuses to touch the modem lines.
            print(f"[probe] could not toggle DTR/RTS ({exc}); reading as-is")

    deadline = time.time() + seconds
    raw = bytearray()
    while time.time() < deadline:
        chunk = ser.read(4096)
        if chunk:
            raw += chunk
            sys.stdout.write(chunk.decode("utf-8", "replace"))
            sys.stdout.flush()

    ser.close()

    log = pathlib.Path(__file__).resolve().parent.parent / "serial_log.txt"
    log.write_bytes(bytes(raw))
    print(f"\n[probe] {len(raw)} bytes captured -> {log}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
