#!/usr/bin/env python3
"""Watch what happens between "USB cable inserted" and "drive letter appears".

Two independent clocks are recorded:

  PC side   - poll the serial-port list for the device's CDC interface (that
              marks the end of enumeration) and the drive-letter bitmap (that
              marks the end of the storage mount).  Both are polled every
              50 ms, so the split between the two phases is visible without
              any cooperation from the firmware.

  device    - once the CDC port is back, the firmware's event ring is dumped
              with the 'mtrace' command.  Its timestamps are ms since reset,
              so they say which SCSI command the host was waiting on.

Run it, plug the board in when prompted, read the report.

    py -3 tools/watch_mount.py [timeout_s]
"""
import ctypes
import os
import re
import sys
import threading
import time

import serial
from serial.tools import list_ports

VID = 0xCAFE          # usb_descriptors.c
PID_MASK = 0x4000     # CDC|MSC composite -> 0x4003
POLL = 0.05


def drive_letters():
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    return {chr(ord("A") + i) for i in range(26) if mask & (1 << i)}


def usable_letters():
    """Letters that can actually be opened and listed.

    A drive letter can linger after an unplug (Windows keeps the mount point
    but the volume is gone), so existence alone is not a usable signal.
    """
    ok = set()
    for L in "DEFGHIJKLMNOPQRSTUVWXYZ":
        try:
            os.listdir(L + ":\\")
            ok.add(L)
        except OSError:
            pass
    return ok


def cdc_port():
    for p in list_ports.comports():
        if p.vid == VID and (p.pid or 0) & PID_MASK == PID_MASK:
            return p.device
    return None


def dump_trace(port):
    try:
        s = serial.Serial(port, 115200, timeout=0.25)
    except Exception as e:                                   # noqa: BLE001
        return "cannot open %s: %r" % (port, e)
    try:
        s.dtr = True
        time.sleep(0.6)
        s.reset_input_buffer()
        s.write(b"mtrace\r")
        buf = b""
        t0 = time.time()
        while time.time() - t0 < 10:
            got = s.read(16384)
            if got:
                buf += got
                if b"last HAL SD error" in buf:
                    break
        return buf.decode(errors="replace")
    finally:
        s.close()


def main():
    timeout = 180.0
    for a in sys.argv[1:]:
        if a.isdigit():
            timeout = float(a)
    want_unplug = "--unplug" in sys.argv

    if want_unplug:
        print("waiting for the device to be unplugged ...")
        t = time.time()
        while time.time() - t < 60:
            if cdc_port() is None:
                break
            time.sleep(0.2)
        time.sleep(1.0)          # let Windows drop the volume as well
        print("  gone (after %.1f s)\n" % (time.time() - t))

    base_drives = drive_letters() & usable_letters()
    print("baseline usable : %s" % " ".join(sorted(base_drives)))
    print("CDC port now    : %s" % (cdc_port() or "absent"))
    print("watching for %.0f s - plug the board in now\n" % timeout)

    t0 = time.time()
    port_seen = None
    port_at = None
    new_drive = None
    drive_at = None
    last_note = 0.0

    # The debug UART (ST-Link VCP) stays up across the plug, so its log lines
    # give a wall-clock anchor for the firmware's "ms since reset" stamps.
    stop = threading.Event()
    uart = []

    def uart_reader():
        try:
            s = serial.Serial("COM19", 115200, timeout=0.1)
        except Exception as e:                              # noqa: BLE001
            uart.append((0.0, "COM19 open failed: %r" % (e,)))
            return
        while not stop.is_set():
            d = s.read(4096)
            if d:
                for ln in d.decode(errors="replace").replace("\r", "\n").split("\n"):
                    ln = ln.strip()
                    if ln:
                        uart.append((time.time() - t0, ln))
        s.close()

    threading.Thread(target=uart_reader, daemon=True).start()

    while time.time() - t0 < timeout:
        now = time.time() - t0

        if port_at is None:
            p = cdc_port()
            if p:
                port_at, port_seen = now, p
                print("[%7.3f s] CDC port up       : %s  (enumeration done)"
                      % (now, p))

        if drive_at is None:
            fresh = usable_letters() - base_drives
            if fresh:
                drive_at, new_drive = now, sorted(fresh)[0]
                print("[%7.3f s] volume usable     : %s:  (can list root)"
                      % (now, new_drive))

        if drive_at is not None and port_at is not None:
            break

        if now - last_note > 2.0:
            last_note = now
            print("[%7.3f s] waiting ... port=%s drive=%s"
                  % (now, port_seen or "-", new_drive or "-"))

        time.sleep(POLL)

    # Phase 3: the volume is mounted, but can it actually be *opened*?  The
    # perceived "plug-in time" is this step, not the drive letter.
    t_list = None
    names = []
    if new_drive is not None:
        deadline = time.time() + 60.0
        while time.time() < deadline:
            try:
                names = os.listdir(new_drive + ":\\")
                if names:
                    t_list = time.time() - t0
                    print("[%7.3f s] root listed      : %s:  (%d entries)"
                          % (t_list, new_drive, len(names)))
                    break
            except OSError:
                pass
            time.sleep(POLL)

    # Phase 4: first byte out of a real file (what double-clicking does).
    t_file = None
    fbytes = 0
    if names:
        try:
            t_start = time.time()
            with open(os.path.join(new_drive + ":\\", names[0]), "rb") as f:
                fbytes = len(f.read(4096))
            t_file = time.time() - t0
            print("[%7.3f s] first file read  : %s  (%d bytes, %.0f ms in call)"
                  % (t_file, names[0], fbytes, (time.time() - t_start) * 1000))
        except OSError as e:                                # noqa: BLE001
            print("first file read failed: %r" % (e,))

    print()
    stop.set()
    if uart:
        print("=== debug UART (wall clock -> firmware log) ===")
        for at, ln in uart:
            print("  [%7.3f s] %s" % (at, ln))
        print()

    if port_at is not None and drive_at is not None:
        print("=== PC-side timeline ===")
        print("  enumeration (plug -> CDC port) : %6.0f ms" % (port_at * 1000))
        print("  mount       (CDC port -> drive): %6.0f ms"
              % ((drive_at - port_at) * 1000))
        if t_list is not None:
            print("  openable    (drive -> root)    : %6.0f ms"
                  % ((t_list - drive_at) * 1000))
            print("  ------------------------------------------")
            print("  PLUG -> USABLE (root listed)   : %6.0f ms" % (t_list * 1000))
        if t_file is not None:
            print("  first file readable at         : %6.0f ms" % (t_file * 1000))
    else:
        print("=== incomplete: port=%s drive=%s ===" % (port_at, drive_at))

    if port_seen:
        print("\n=== device-side trace ===")
        txt = dump_trace(port_seen)
        print(txt)
        rows = re.findall(r"^\s*(\d+) ms\s+(\S+).*$", txt, re.M)
        if rows:
            first, last = int(rows[0][0]), int(rows[-1][0])
            print("trace covers %d ms, %d events, last event at %d ms"
                  % (last - first, len(rows), last))


if __name__ == "__main__":
    main()
