#!/usr/bin/env python3
"""
End-to-end console test for the STM32H743 LVGL menu + NES emulator firmware.

The board has no push-buttons: every UI action is a text command on the debug
UART (USART1 via the ST-Link VCP) or on the USB CDC port.  Both links share the
same parser, so this script works against either - just point --port at the
right COM number.

Protocol
--------
Every command answers with exactly one line starting with "OK " or "ERR ",
optionally preceded by payload lines.  That is all the framing this script
needs: send a line, read until OK/ERR, done.

Usage
-----
    python scripts/serial_test.py --port COM19              # full self-test
    python scripts/serial_test.py --port COM19 --list       # only list ROMs
    python scripts/serial_test.py --port COM19 --play 0     # run ROM #0
    python scripts/serial_test.py --port COM19 --interactive
"""

import argparse
import sys
import time

try:
    import serial                      # pyserial
    from serial.tools import list_ports
except ImportError:                    # pragma: no cover
    sys.exit("pyserial is missing:  python -m pip install pyserial")


# ---------------------------------------------------------------------------
#  Console wrapper
# ---------------------------------------------------------------------------

class Console:
    """Line oriented view of the board's serial console."""

    def __init__(self, port, baud=115200, timeout=1.0, verbose=False):
        self.verbose = verbose
        self.ser = serial.Serial(port, baud, timeout=timeout)
        # The USB CDC port needs DTR asserted before the firmware considers
        # the host present; harmless on the ST-Link VCP.
        self.ser.dtr = True
        time.sleep(0.15)
        self.ser.reset_input_buffer()

    def close(self):
        try:
            self.send("release")       # never leave a key stuck down
        except Exception:
            pass
        self.ser.close()

    # -- raw ---------------------------------------------------------------

    def _write(self, text):
        self.ser.write(text.encode("ascii", "replace"))
        self.ser.flush()

    def _readline(self):
        raw = self.ser.readline()
        if not raw:
            return None
        return raw.decode("utf-8", "replace").rstrip("\r\n")

    # -- protocol ----------------------------------------------------------

    def send(self, cmd, timeout=3.0):
        """Send one command, collect payload lines, return (status, lines).

        status is "OK", "ERR" or "TIMEOUT".
        """
        self.ser.reset_input_buffer()
        self._write(cmd + "\r\n")

        lines = []
        deadline = time.time() + timeout

        while time.time() < deadline:
            line = self._readline()
            if line is None:
                continue
            if self.verbose:
                print("  < " + line)

            # The firmware echoes typed characters unless "echo off" was sent;
            # the echo of our own command comes back first, drop it.
            if line.strip() == cmd.strip():
                continue

            if line.startswith("OK"):
                return "OK", lines
            if line.startswith("ERR"):
                return "ERR", lines + [line]

            if line:
                lines.append(line)

        return "TIMEOUT", lines


# ---------------------------------------------------------------------------
#  Test harness
# ---------------------------------------------------------------------------

class Runner:
    def __init__(self, con):
        self.con = con
        self.passed = 0
        self.failed = 0

    def check(self, name, cmd, expect="OK", show=False, timeout=3.0):
        status, lines = self.con.send(cmd, timeout=timeout)
        good = (status == expect)

        mark = "PASS" if good else "FAIL"
        print("[%s] %-28s -> %s" % (mark, cmd, status))

        if show:
            for line in lines:
                print("       | " + line)

        if good:
            self.passed += 1
        else:
            self.failed += 1
            for line in lines:
                print("       ! " + line)

        return lines

    def summary(self):
        total = self.passed + self.failed
        print("")
        print("-" * 52)
        print("%d/%d checks passed" % (self.passed, total))
        return 0 if self.failed == 0 else 1


# ---------------------------------------------------------------------------
#  Test bodies
# ---------------------------------------------------------------------------

def test_basics(r):
    print("\n== basics ==")
    r.check("echo off",  "echo off")
    r.check("banner",    "status", show=True)
    r.check("page list", "pages",  show=True)


def test_navigation(r):
    """Walk the whole menu: open every page, tick it, come back."""
    print("\n== navigation ==")

    lines = r.con.send("pages")[1]
    handles = []
    for line in lines:
        # "  0 nes        NES 模拟器"   ->  index 1 is the cmd handle
        parts = line.split()
        if len(parts) >= 2 and parts[0].lstrip("*").isdigit():
            handles.append(parts[1])
        elif len(parts) >= 3 and parts[1].isdigit():
            handles.append(parts[2])

    if not handles:
        print("[FAIL] could not parse the page list")
        r.failed += 1
        return

    for h in handles:
        r.check("open " + h, "open " + h)
        time.sleep(0.4)               # let the page paint
        r.check("back from " + h, "menu")
        time.sleep(0.2)


def test_keys(r):
    """Menu movement through the virtual key layer, then a stuck-key check."""
    print("\n== keys ==")

    r.check("select 0", "sel 0")

    for _ in range(3):
        r.check("key down", "key down")
        time.sleep(0.12)

    r.check("key up",  "key up")
    time.sleep(0.12)

    # Held keys: what the emulator's pad actually reads.
    r.check("hold a",    "down a")
    lines = r.con.send("keys")[1]
    held = [l for l in lines if l.endswith("DOWN")]
    if any(l.startswith("a ") for l in held):
        print("[PASS] a is reported held")
        r.passed += 1
    else:
        print("[FAIL] a should be held, got: %s" % held)
        r.failed += 1

    r.check("release a", "up a")
    r.check("release all", "release")


def test_nes(r, rom_index=None, play_seconds=5):
    print("\n== nes ==")

    lines = r.check("rom list", "rom list", show=True, timeout=6.0)

    roms = []
    for line in lines:
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[0].isdigit():
            roms.append((int(parts[0]), parts[1]))

    if not roms:
        print("       (no .nes files on the card - copy some into 1:/NES)")
        return

    index = rom_index if rom_index is not None else roms[0][0]
    r.check("rom load %d" % index, "rom load %d" % index, timeout=8.0)

    # The load happens in the page tick, give the card time.
    time.sleep(1.5)
    r.check("rom info", "rom info", show=True)

    print("       playing for %d s, pressing start/A ..." % play_seconds)
    end = time.time() + play_seconds
    while time.time() < end:
        r.con.send("key start", timeout=1.0)
        time.sleep(0.6)
        r.con.send("key a", timeout=1.0)
        time.sleep(0.6)

    lines = r.check("fps check", "rom info", show=True)
    for line in lines:
        if line.startswith("fps"):
            try:
                fps = int(line.split(":")[1])
            except (IndexError, ValueError):
                break
            if fps >= 20:
                print("[PASS] %d fps" % fps)
                r.passed += 1
            else:
                print("[FAIL] only %d fps - build Release (-O2)?" % fps)
                r.failed += 1
            break

    r.check("rom stop", "rom stop")
    r.check("back to menu", "menu")


def test_image(r, show_index=None):
    """Exercise the image viewer console path: list, decode, view, return."""
    print("\n== image ==")

    lines = r.check("img list", "img list", show=True, timeout=6.0)

    imgs = []
    for line in lines:
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[0].isdigit():
            imgs.append((int(parts[0]), parts[1]))

    if not imgs:
        print("       (no .bmp/.jpg files on the card - copy some into 1:)")
        return

    index = show_index if show_index is not None else imgs[0][0]
    r.check("img show %d" % index, "img show %d" % index, timeout=8.0)

    # The decode happens in the page tick; give the card a moment.
    time.sleep(1.5)
    lines = r.check("img info", "img info", show=True)

    viewing = any("viewing" in l for l in lines)
    if viewing:
        print("[PASS] image #%d decoded and shown" % index)
        r.passed += 1
    else:
        print("[FAIL] image viewer did not enter the viewing state")
        r.failed += 1

    r.check("img close", "img close")
    r.check("back to menu", "menu")


def test_txt(r):
    """TXT reader console path: list, seed if needed, open, page, close, errors."""
    print("\n== txt ==")

    lines = r.check("txt list", "txt list", show=True, timeout=6.0)
    count = 0
    for line in lines:
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[0].isdigit():
            count += 1

    if count == 0:
        r.check("txt seed", "txt seed SEED.TXT", timeout=8.0)
        lines = r.check("txt list", "txt list", show=True, timeout=6.0)
        count = sum(1 for l in lines
                    if len(l.split(None, 1)) == 2 and l.split(None, 1)[0].isdigit())

    if count == 0:
        print("       (no .txt files on the card and seeding failed)")
        return

    r.check("txt open 0", "txt open 0", timeout=8.0)
    time.sleep(1.0)                       # load happens in the page tick
    info = r.check("txt info", "txt info", show=True)
    reading = any("reading" in l for l in info)
    if reading:
        print("[PASS] txt reader entered reading state")
        r.passed += 1
    else:
        print("[FAIL] txt reader did not enter reading state")
        r.failed += 1

    # Paging must not wedge the console.
    r.check("key down", "key down", timeout=4.0)
    r.check("txt info", "txt info", timeout=4.0)
    r.check("key up", "key up", timeout=4.0)
    r.check("txt info", "txt info", timeout=4.0)

    r.check("txt close", "txt close", timeout=4.0)
    r.check("back to menu", "menu", timeout=4.0)

    # Error path: out-of-range index must be rejected, not wedged.
    r.check("txt open 99", "txt open 99", expect="ERR", timeout=4.0)


def test_errors(r):
    """The parser must reject nonsense instead of wedging."""
    print("\n== error handling ==")
    r.check("bad command", "wibble",        expect="ERR")
    r.check("bad key",     "key nosuchkey", expect="ERR")
    r.check("bad page",    "open nosuch",   expect="ERR")
    r.check("bad rom",     "rom load 999",  expect="ERR")
    r.check("bad img",     "img show 999",  expect="ERR")
    r.check("still alive", "status")


# ---------------------------------------------------------------------------
#  Interactive mode
# ---------------------------------------------------------------------------

def interactive(con):
    print("Interactive console.  Ctrl-C or 'quit' to leave.")
    print("Try: help / pages / open nes / rom list / rom load 0 / key a")
    con.send("echo off")

    try:
        while True:
            try:
                cmd = input("board> ").strip()
            except EOFError:
                break
            if cmd in ("quit", "exit"):
                break
            if not cmd:
                continue
            status, lines = con.send(cmd, timeout=6.0)
            for line in lines:
                print(line)
            print(status)
    except KeyboardInterrupt:
        print("")


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------

def pick_port():
    ports = list(list_ports.comports())
    if not ports:
        return None
    for p in ports:
        text = "%s %s" % (p.description or "", p.manufacturer or "")
        if "STLink" in text or "ST-Link" in text or "STMicro" in text:
            return p.device
    return ports[0].device


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: first ST-Link VCP)")
    ap.add_argument("--baud", type=int, default=115200,
                    help="baud rate, ignored by the USB CDC port")
    ap.add_argument("--list", action="store_true", help="only list ROMs")
    ap.add_argument("--play", type=int, metavar="N",
                    help="load ROM N and run it for a few seconds")
    ap.add_argument("--seconds", type=int, default=5,
                    help="how long --play keeps the game running")
    ap.add_argument("--interactive", action="store_true",
                    help="type commands by hand")
    ap.add_argument("--ports", action="store_true",
                    help="list the serial ports on this machine and exit")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="show every line the board sends")
    args = ap.parse_args()

    if args.ports:
        for p in list_ports.comports():
            print("%-8s %s" % (p.device, p.description))
        return 0

    port = args.port or pick_port()
    if not port:
        return "no serial port found - is the board plugged in?"

    print("Connecting to %s @ %d ..." % (port, args.baud))

    try:
        con = Console(port, args.baud, verbose=args.verbose)
    except serial.SerialException as exc:
        return "cannot open %s: %s" % (port, exc)

    try:
        if args.interactive:
            interactive(con)
            return 0

        r = Runner(con)

        if args.list:
            con.send("echo off")
            r.check("rom list", "rom list", show=True, timeout=6.0)
            return r.summary()

        if args.play is not None:
            con.send("echo off")
            test_nes(r, rom_index=args.play, play_seconds=args.seconds)
            return r.summary()

        test_basics(r)
        test_navigation(r)
        test_keys(r)
        test_nes(r, play_seconds=args.seconds)
        test_image(r)
        test_txt(r)
        test_errors(r)
        return r.summary()
    finally:
        con.close()


if __name__ == "__main__":
    sys.exit(main())
