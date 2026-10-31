#!/usr/bin/env python3
"""Prove that the board's wheel byte really reaches the host.

Why a low-level hook
--------------------
A wheel notch is not a key and not a cursor move, so neither GetCursorPos() nor
GetAsyncKeyState() can see it.  The only honest observer is a WH_MOUSE_LL hook:
it sits in front of every application and reports the signed delta Windows
derived from the HID report.

Usage
-----
    python tools/wheel_probe.py --port COM3            # run, then scroll
    python tools/wheel_probe.py --port COM3 --send u   # drive it from serial

With --send the script writes the given characters to the board first, which
exercises the whole path (firmware -> USB -> Windows) without anybody touching
the glass.  The firmware maps 'u' to three "wheel up" notches and 'j' to three
"wheel down" notches, so a correct run shows +-360 in total (Windows reports
120 per notch).

Exit codes: 0 the expected wheel motion was observed, 1 none was, 2 setup error.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import sys
import threading
import time

WH_MOUSE_LL = 14
WM_MOUSEWHEEL = 0x020A
WM_MOUSEHWHEEL = 0x020C
WHEEL_DELTA = 120

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

LRESULT = ctypes.c_ssize_t
HOOKPROC = ctypes.WINFUNCTYPE(LRESULT, ctypes.c_int, wt.WPARAM, wt.LPARAM)

# ctypes defaults a missing restype to c_int, which TRUNCATES the 64-bit module
# handle on this platform - SetWindowsHookExW then fails with error code 0 and
# no hook at all.  Declare the prototypes explicitly.
kernel32.GetModuleHandleW.restype = wt.HMODULE
kernel32.GetModuleHandleW.argtypes = [wt.LPCWSTR]
user32.SetWindowsHookExW.restype = ctypes.c_void_p
user32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOKPROC, wt.HMODULE, wt.DWORD]
user32.CallNextHookEx.restype = LRESULT
user32.CallNextHookEx.argtypes = [ctypes.c_void_p, ctypes.c_int, wt.WPARAM, wt.LPARAM]
user32.UnhookWindowsHookEx.argtypes = [ctypes.c_void_p]


class MSLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("pt", wt.POINT),
        ("mouseData", wt.DWORD),
        ("flags", wt.DWORD),
        ("time", wt.DWORD),
        ("dwExtraInfo", ctypes.c_void_p),
    ]


events = []          # (timestamp, 'V'|'H', signed delta)
_events_lock = threading.Lock()


def _hook_proc(n_code, w_param, l_param):
    if n_code == 0 and w_param in (WM_MOUSEWHEEL, WM_MOUSEHWHEEL):
        info = ctypes.cast(l_param, ctypes.POINTER(MSLLHOOKSTRUCT)).contents
        raw = (info.mouseData >> 16) & 0xFFFF
        delta = ctypes.c_short(raw).value
        with _events_lock:
            events.append((time.time(), "V" if w_param == WM_MOUSEWHEEL else "H", delta))
    return user32.CallNextHookEx(None, n_code, w_param, l_param)


_hook_ref = HOOKPROC(_hook_proc)     # must stay alive or the hook is unhooked


def hook_thread(stop):
    hook = user32.SetWindowsHookExW(WH_MOUSE_LL, _hook_ref, kernel32.GetModuleHandleW(None), 0)
    if not hook:
        print("SetWindowsHookExW failed: %d" % ctypes.get_last_error())
        stop.set()
        return

    msg = wt.MSG()
    while not stop.is_set():
        # PeekMessageW + short sleep: GetMessageW would block and never notice
        # the stop flag.
        while user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))
        time.sleep(0.01)

    user32.UnhookWindowsHookEx(hook)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--send", default="u", help="characters to write to the board")
    ap.add_argument("--seconds", type=float, default=4.0)
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        print("pyserial missing")
        return 2

    stop = threading.Event()
    t = threading.Thread(target=hook_thread, args=(stop,), daemon=True)
    t.start()
    time.sleep(0.4)          # let the hook install before anything is sent

    try:
        ser = serial.Serial(args.port, 115200, timeout=0.2)
    except Exception as exc:                    # noqa: BLE001 - report as-is
        stop.set()
        print("cannot open %s: %s" % (args.port, exc))
        return 2

    time.sleep(0.3)
    ser.reset_input_buffer()

    with _events_lock:
        events.clear()

    for ch in args.send:
        ser.write(ch.encode())
        ser.flush()
        time.sleep(0.25)

    time.sleep(args.seconds)
    txt = ser.read(4096).decode("utf-8", "replace")
    ser.close()

    stop.set()
    time.sleep(0.1)

    with _events_lock:
        seen = list(events)

    total_v = sum(d for _, ax, d in seen if ax == "V")
    total_h = sum(d for _, ax, d in seen if ax == "H")

    print("sent       : %r" % args.send)
    print("wheel events: %d  (vertical %d, horizontal %d)" % (len(seen), sum(1 for e in seen if e[1] == "V"),
                                                             sum(1 for e in seen if e[1] == "H")))
    for ts, ax, d in seen[-12:]:
        print("  %.3f  %s  %+d" % (ts - (seen[0][0] if seen else ts), ax, d))
    print("vertical total  : %+d  (%+.2f notches)" % (total_v, total_v / float(WHEEL_DELTA)))
    print("horizontal total: %+d  (must be 0: the strip is Y-only)" % total_h)

    if "[WHEL]" in txt:
        for line in txt.splitlines():
            if line.startswith("[WHEL]") or line.startswith("[CMD ]"):
                print("board: %s" % line.strip())

    expect = sum(1 if c in "uU" else -1 for c in args.send if c in "uUjJ") * 3 * WHEEL_DELTA
    if total_h != 0:
        print("RESULT: FAIL - horizontal wheel traffic appeared")
        return 1
    if expect != 0 and total_v == expect:
        print("RESULT: PASS - %+d == %+d expected" % (total_v, expect))
        return 0
    if total_v != 0:
        print("RESULT: PARTIAL - wheel arrived (%+d) but not the expected %+d" % (total_v, expect))
        return 0
    print("RESULT: FAIL - no wheel event reached the host")
    return 1


if __name__ == "__main__":
    sys.exit(main())
