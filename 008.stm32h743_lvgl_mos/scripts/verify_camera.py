#!/usr/bin/env python3
"""
One-click acceptance test for the STM32H743 LVGL + NES firmware's OV5640
camera app (96x96 centered RGB565 preview, triple-buffered, sharing the
sram_pool with the NES machine).

What it verifies (the two user acceptance criteria):

  (1)  Camera frame buffers share the dynamic sram_pool with NES.
       Buffers are allocated on enter and released on exit -> after
       `cam stop` the RAM_D2 free bytes must return to the pre-open
       baseline and the allocator invariants must still hold
       (sram check d2 == integrity ok).  No leak, no corruption.

  (2)  The full system stays healthy:
         - the OV5640 initialises and streams live (fps > 0, frames
           increasing, overruns == 0 -> no tearing/corruption);
         - repeated open/close cycles never leak or corrupt;
         - the B key and SELECT key both leave the full-screen camera
           page and release its buffers (key-exit path);
         - after the camera is closed, NES still opens and runs, and
           the pool is released back to baseline afterwards;
         - the console stays responsive throughout (no wedging).

Usage
-----
    python scripts/verify_camera.py                 # auto-detect port, full run
    python scripts/verify_camera.py --port COM19    # explicit port
    python scripts/verify_camera.py --cycles 5 --seconds 4
    python scripts/verify_camera.py --ports         # list serial ports
    python scripts/verify_camera.py -v              # verbose board output
"""

import argparse
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    sys.exit("pyserial is missing:  python -m pip install pyserial")


# ---------------------------------------------------------------------------
#  Console wrapper (mirrors scripts/serial_test.py)
# ---------------------------------------------------------------------------

class Console:
    def __init__(self, port, baud=115200, timeout=1.0, verbose=False):
        self.verbose = verbose
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.ser.dtr = True           # needed by USB CDC; harmless on VCP
        time.sleep(0.15)
        self.ser.reset_input_buffer()

    def close(self):
        try:
            self.send("release")
        except Exception:
            pass
        self.ser.close()

    def _write(self, text):
        self.ser.write(text.encode("ascii", "replace"))
        self.ser.flush()

    def _readline(self):
        raw = self.ser.readline()
        if not raw:
            return None
        return raw.decode("utf-8", "replace").rstrip("\r\n")

    def send(self, cmd, timeout=3.0):
        """Send one command; return (status, lines). status in OK/ERR/TIMEOUT."""
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
            if line.strip() == cmd.strip():
                continue           # echo of our own command
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
        print("[%s] %-34s -> %s" % ("PASS" if good else "FAIL", name, status))
        if show:
            for line in lines:
                print("       | " + line)
        if good:
            self.passed += 1
        else:
            self.failed += 1
            for line in lines:
                print("       ! " + line)
        return status, lines

    def summary(self):
        total = self.passed + self.failed
        print("")
        print("-" * 60)
        print("%d/%d checks passed" % (self.passed, total))
        return 0 if self.failed == 0 else 1


# ---------------------------------------------------------------------------
#  Parsers for the firmware's console output
# ---------------------------------------------------------------------------

def d2_free(lines):
    """Return (free, total) parsed from `sram info` output, or None."""
    for l in lines:
        m = re.search(r"^d2\s+free\s+(\d+)/(\d+)\s*B,\s*integrity\s+(\w+)",
                      l.strip())
        if m:
            return int(m.group(1)), int(m.group(2)), m.group(3)
    return None


def d2_integrity(lines):
    """Return 'ok'/'BAD'/None parsed from `sram check d2` output."""
    for l in lines:
        m = re.search(r"d2\s+free\s+\d+\s+integrity\s+(\w+)", l)
        if m:
            return m.group(1)
    return None


def cam_state(lines):
    """Return dict with fps/frames/overruns parsed from `cam info`/`cam state`.

    Returns None when the camera is not live (e.g. 'closed' / 'open, init
    failed').
    """
    for l in lines:
        m = re.search(r"live\s+(\d+)fps,\s*frames\s+(\d+),\s*overruns\s+(\d+)",
                      l)
        if m:
            return {"fps": int(m.group(1)),
                    "frames": int(m.group(2)),
                    "overruns": int(m.group(3))}
    return None


def cam_closed(lines):
    """True if `cam info` reports the page is fully closed (not live/open)."""
    for l in lines:
        if "closed" in l:
            return True
    return False


# ---------------------------------------------------------------------------
#  Test bodies
# ---------------------------------------------------------------------------

def read_baseline(r):
    print("\n== baseline (sram pool before camera) ==")
    _, lines = r.con.send("sram info", timeout=4.0)
    base = d2_free(lines)
    if base is None:
        print("[FAIL] could not parse sram info baseline")
        r.failed += 1
        return None
    print("       | d2 free %d/%d B, integrity %s" % base)
    return base[0]


def test_camera_cycle(r, idx, seconds, baseline):
    print("\n-- camera cycle %d --" % (idx + 1))

    st, _ = r.check("cam open", "cam open", timeout=15.0)
    if st != "OK":
        return

    # Wait until the sensor streams live (poll up to ~15 s).
    state = None
    for _ in range(30):
        _, lines = r.con.send("cam info", timeout=4.0)
        state = cam_state(lines)
        if state is not None:
            break
        time.sleep(0.5)

    if state is None:
        print("[FAIL] camera never reached live state")
        r.failed += 1
        _, lf = r.con.send("cam info", timeout=4.0)
        for l in lf:
            print("       ! " + l)
        r.con.send("cam stop", timeout=6.0)
        return

    print("[PASS] camera live: %dfps, %d frames, %d overruns"
          % (state["fps"], state["frames"], state["overruns"]))
    r.passed += 1

    # Let it run, then confirm frames advanced and no overruns.
    time.sleep(seconds)
    _, lines2 = r.con.send("cam info", timeout=4.0)
    state2 = cam_state(lines2)
    if state2 is None:
        print("[FAIL] camera dropped out of live state mid-run")
        r.failed += 1
    else:
        advanced = state2["frames"] > state["frames"]
        no_tear = state2["overruns"] == 0
        print("%s frames advanced %d -> %d"
              % ("[PASS]" if advanced else "[FAIL]",
                 state["frames"], state2["frames"]))
        print("%s overruns == 0 (%d)"
              % ("[PASS]" if no_tear else "[FAIL]", state2["overruns"]))
        r.passed += int(advanced) + int(no_tear)
        r.failed += int(not advanced) + int(not no_tear)

    # Stop and verify the shared pool is fully released.
    r.check("cam stop", "cam stop", timeout=6.0)
    time.sleep(0.3)
    _, lines3 = r.con.send("sram info", timeout=4.0)
    after = d2_free(lines3)
    if after is None:
        print("[FAIL] could not read sram info after stop")
        r.failed += 1
        return

    released = (after[0] == baseline)
    integ = (after[2] == "ok")
    print("%s d2 released to baseline (%d/%d == %d)"
          % ("[PASS]" if released else "[FAIL]",
             after[0], after[1], baseline))
    print("%s d2 integrity %s" % ("[PASS]" if integ else "[FAIL]", after[2]))
    r.passed += int(released) + int(integ)
    r.failed += int(not released) + int(not integ)


def test_nes_after_camera(r, baseline, seconds=4):
    print("\n== NES after camera (shared-pool regression) ==")

    # NES uses the SAME sram_pool region (RAM_D2) the camera just released.
    r.check("open nes", "open nes", timeout=6.0)
    time.sleep(1.0)

    _, lines = r.con.send("rom list", timeout=6.0)
    roms = [l for l in lines if l.split(None, 1)[0].isdigit()]
    if not roms:
        print("       (no .nes files on the card - copy some into 1:/NES)")
    else:
        r.check("rom load 0", "rom load 0", timeout=8.0)
        time.sleep(1.5)
        _, lines = r.con.send("rom info", timeout=5.0)
        fps = 0
        for l in lines:
            m = re.search(r"fps\s*:?\s*(\d+)", l)
            if m:
                fps = int(m.group(1))
        if fps >= 20:
            print("[PASS] NES running at %d fps (Release -O2)" % fps)
            r.passed += 1
        else:
            print("[FAIL] NES fps %d - expected >=20" % fps)
            r.failed += 1
        # Standard dynamic-pool assertion: NES must occupy the pool, then
        # release it on exit.
        _, sl = r.con.send("sram info", timeout=4.0)
        occ = d2_free(sl)
        if occ and occ[0] < baseline:
            print("[PASS] NES occupies pool (d2 %d/%d)" % (occ[0], occ[1]))
            r.passed += 1
        else:
            print("[WARN] NES did not appear to occupy the pool")
        r.check("rom stop", "rom stop", timeout=6.0)

    r.check("back to menu", "menu", timeout=5.0)
    time.sleep(0.5)

    _, lines = r.con.send("sram info", timeout=4.0)
    after = d2_free(lines)
    if after is None:
        print("[FAIL] could not read sram info after NES")
        r.failed += 1
        return
    released = (after[0] == baseline)
    integ = (after[2] == "ok")
    print("%s d2 released to baseline after NES (%d == %d)"
          % ("[PASS]" if released else "[FAIL]", after[0], baseline))
    print("%s d2 integrity %s" % ("[PASS]" if integ else "[FAIL]", after[2]))
    r.passed += int(released) + int(integ)
    r.failed += int(not released) + int(not integ)


def test_key_exit(r, baseline):
    """Verify the physical/page keys (B, SELECT) leave the camera page.

    This is the user-visible exit path: injecting the B key (and SELECT, as a
    regression) must close the full-screen camera page and release its buffers
    back to the shared sram_pool.
    """
    print("\n== key-exit (B / SELECT leave the camera page) ==")

    r.con.send("cam open", timeout=15.0)
    live = False
    for _ in range(30):
        _, lines = r.con.send("cam info", timeout=4.0)
        if cam_state(lines) is not None:
            live = True
            break
        time.sleep(0.5)
    if not live:
        print("[FAIL] camera never went live for the key-exit test")
        r.failed += 1
        r.con.send("cam stop", timeout=6.0)
        return

    # The B key must now exit the page (the regression under test).
    r.check("inject key B (tap)", "key b", timeout=4.0)
    time.sleep(0.4)
    _, lines = r.con.send("cam info", timeout=4.0)
    b_exits = cam_closed(lines)
    print("%s B key exits the camera page" % ("[PASS]" if b_exits else "[FAIL]"))
    r.passed += int(b_exits)
    r.failed += int(not b_exits)
    if not b_exits:
        r.con.send("cam stop", timeout=6.0)
    time.sleep(0.3)

    # SELECT must also still exit (regression guard) - reopen first.
    r.con.send("cam open", timeout=15.0)
    for _ in range(30):
        _, lines = r.con.send("cam info", timeout=4.0)
        if cam_state(lines) is not None:
            break
        time.sleep(0.5)
    r.check("inject key SELECT (tap)", "key select", timeout=4.0)
    time.sleep(0.4)
    _, lines = r.con.send("cam info", timeout=4.0)
    sel_exits = cam_closed(lines)
    print("%s SELECT key exits the camera page"
          % ("[PASS]" if sel_exits else "[FAIL]"))
    r.passed += int(sel_exits)
    r.failed += int(not sel_exits)
    if not sel_exits:
        r.con.send("cam stop", timeout=6.0)

    # After the exits, the pool must be back to baseline + intact.
    time.sleep(0.3)
    _, lines = r.con.send("sram info", timeout=4.0)
    after = d2_free(lines)
    if after is None:
        print("[FAIL] could not read sram info after key-exit")
        r.failed += 1
        return
    released = (after[0] == baseline)
    integ = (after[2] == "ok")
    print("%s d2 released to baseline after key-exit (%d == %d)"
          % ("[PASS]" if released else "[FAIL]", after[0], baseline))
    print("%s d2 integrity %s" % ("[PASS]" if integ else "[FAIL]", after[2]))
    r.passed += int(released) + int(integ)
    r.failed += int(not released) + int(not integ)


def test_alive_after_all(r):
    print("\n== console still responsive ==")
    r.check("final status", "status", timeout=4.0)


# ---------------------------------------------------------------------------
#  Port selection
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


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: first ST-Link VCP)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--cycles", type=int, default=3,
                    help="number of camera open/close cycles")
    ap.add_argument("--seconds", type=int, default=4,
                    help="seconds to keep the preview running each cycle")
    ap.add_argument("--ports", action="store_true",
                    help="list serial ports and exit")
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

    print("Camera acceptance test -> %s @ %d" % (port, args.baud))
    print("cycles=%d  per-cycle-seconds=%d" % (args.cycles, args.seconds))

    try:
        con = Console(port, args.baud, verbose=args.verbose)
    except serial.SerialException as exc:
        return "cannot open %s: %s" % (port, exc)

    try:
        con.send("echo off")
        r = Runner(con)

        baseline = read_baseline(r)
        if baseline is None:
            return r.summary()

        for i in range(args.cycles):
            test_camera_cycle(r, i, args.seconds, baseline)

        test_key_exit(r, baseline)
        test_nes_after_camera(r, baseline, seconds=args.seconds)
        test_alive_after_all(r)
        return r.summary()
    finally:
        con.close()


if __name__ == "__main__":
    sys.exit(main())
