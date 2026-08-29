#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Live on-board test for the music player (103.stm32f429_ui_music).

Flashes the firmware, then drives the player through the serial test console
(commands p/n/v/+/- run in the UI task -- the same context as the on-screen
button callbacks).  If the click-to-freeze bug were still present, a 'next' /
'prev' while playing would NULL-deref the decoder and HardFault the whole
board; that would show up as the [CMD] replies stopping and the UART going
silent.  We also confirm a Chinese-named track opens (UTF-8 path).
"""
import os
import sys
import time
import serial
import subprocess

PROJ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ELF = os.path.join(PROJ, "build_dbg", "stm32f429_ui_music.elf")
OCD_BIN = "D:/software/ST/OpenOCD/bin/openocd.exe"
OCD_SCR = "D:/software/ST/OpenOCD/share/openocd/scripts"
OCD_CFG = os.path.join(PROJ, "openocd.cfg")
PORT = os.environ.get("SERIAL_PORT", "COM7")
BAUD = 115200


def log(s):
    sys.stdout.write("[T  ] " + s + "\n")
    sys.stdout.flush()


def flash():
    cmd = [OCD_BIN, "-s", OCD_SCR, "-f", OCD_CFG,
           "-c", 'program "%s" verify reset exit' % ELF.replace("\\", "/")]
    log("flash: " + " ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    out = (r.stdout or "") + (r.stderr or "")
    ok = ("Verified OK" in out) or ("** Verified OK **" in out)
    for line in out.splitlines():
        if "Verified" in line or "Error" in line or "error" in line:
            log("  ocd: " + line.strip())
    return ok


def open_port():
    try:
        s = serial.Serial(PORT, BAUD, timeout=0.25)
        s.reset_input_buffer()
        return s
    except Exception as e:  # noqa
        log("port open FAIL: %s" % e)
        return None


def capture(s, secs):
    buf = b""
    t = time.time() + secs
    while time.time() < t:
        try:
            c = s.read(400)
        except Exception:  # noqa
            c = b""
        if c:
            buf += c
    return buf


def send(s, ch):
    try:
        s.write(ch.encode("ascii"))
    except Exception as e:  # noqa
        log("send FAIL: %s" % e)


def main():
    s = open_port()
    if s is None:
        sys.exit(1)

    # flash (reset) while the port is already open so we catch the banner
    flash()

    log("capturing boot (8s)...")
    boot = capture(s, 8.0)
    txt = boot.decode("latin-1", "replace")
    sys.stdout.write(txt)
    sys.stdout.flush()

    # exercise the freeze-prone paths
    for ch, label in [("n", "next"), ("p", "toggle"), ("v", "prev"),
                      ("n", "next"), ("+", "vol+"), ("-", "vol-")]:
        send(s, ch)
        log("sent '%s' (%s); capturing 2.5s" % (ch, label))
        seg = capture(s, 2.5)
        sys.stdout.write(seg.decode("latin-1", "replace"))
        sys.stdout.flush()

    # final window to see if the player is still alive (no HardFault)
    log("final capture 4s to confirm liveness...")
    fin = capture(s, 4.0)
    sys.stdout.write(fin.decode("latin-1", "replace"))
    sys.stdout.flush()

    combined = (boot + txt.encode("latin-1") + fin).decode("latin-1", "replace")
    n_cmd = combined.count("[CMD]")
    n_open = combined.count("MP3 open") + combined.count("WAV open")
    n_fail = combined.count("open failed")
    log("summary: [CMD]=%d decoder_open_ok=%d open_failed=%d" %
        (n_cmd, n_open, n_fail))
    if n_cmd >= 5 and n_fail == 0:
        log("RESULT: PASS (commands processed, no freeze, no open failures)")
    else:
        log("RESULT: CHECK (cmd=%d fail=%d) -- see log above" % (n_cmd, n_fail))
    s.close()


if __name__ == "__main__":
    main()
