"""Watch the host cursor and the board's own counters at the same time.

Purpose: prove that a finger on the glass actually reaches the PC pointer.  The
board reports how far it *thinks* it moved (MOVE x/y from the serial console),
the host cursor tells whether Windows really received the HID reports.  A
touchpad that is "detected but dead" shows up as MOVE>0 with a frozen cursor;
a mis-parsed coordinate shows up as MOVE==0 (everything clamped out of the pad).

Usage:
    python tools/touch_pointer_probe.py --port COM3 --seconds 45

Leave the window running, swipe a finger across the pad, then read the summary.
Pure ctypes, no Add-Type, so it also works where PowerShell type loading is
blocked.
"""

import argparse
import ctypes
import ctypes.wintypes
import time

import serial


def cursor_pos():
    pt = ctypes.wintypes.POINT()
    if not ctypes.windll.user32.GetCursorPos(ctypes.byref(pt)):
        raise OSError("GetCursorPos failed")
    return pt.x, pt.y


VK_LBUTTON = 0x01


def left_button_down():
    """True while the host thinks the physical left button is held.

    GetAsyncKeyState's low bit is "pressed since the last call", which is what
    makes it usable for a 40 ms synthetic click: sampling at ~1 ms catches the
    edge even though the press is far shorter than a human one.
    """
    return (ctypes.windll.user32.GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0


def parse_move(text):
    """Return (x_total, y_total) from the console's [MOVE] line, or None."""
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("[MOVE]"):
            continue
        xs = ys = None
        for tok in line[6:].split():
            if tok.startswith("x="):
                xs = int(tok[2:])
            elif tok.startswith("y="):
                ys = int(tok[2:])
        if xs is not None and ys is not None:
            return xs, ys
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--seconds", type=float, default=45.0)
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    time.sleep(1.5)
    ser.reset_input_buffer()

    start = time.time()
    cursor_start = cursor_pos()
    cursor_min = list(cursor_start)
    cursor_max = list(cursor_start)
    samples = 0
    next_poll = start
    last_move = (0, 0)
    last_pad = ""

    click_edges = 0
    prev_down = left_button_down()

    print("host cursor at start: (%d, %d)" % cursor_start)
    print("swipe a finger across the pad for ~3 s, then tap it 3 times ...")

    while time.time() - start < args.seconds:
        now = time.time()
        if now >= next_poll:
            next_poll = now + 2.0
            ser.reset_input_buffer()
            ser.write(b"s")
            ser.flush()
            time.sleep(0.35)
            txt = ser.read(4096).decode("utf-8", "replace")
            mv = parse_move(txt)
            if mv is not None:
                last_move = mv
            for line in txt.splitlines():
                if line.strip().startswith("[PAD]"):
                    last_pad = line.strip()

        down = left_button_down()
        if down and not prev_down:
            click_edges += 1
        prev_down = down

        x, y = cursor_pos()
        samples += 1
        cursor_min[0] = min(cursor_min[0], x)
        cursor_min[1] = min(cursor_min[1], y)
        cursor_max[0] = max(cursor_max[0], x)
        cursor_max[1] = max(cursor_max[1], y)
        time.sleep(0.004)

    cursor_end = cursor_pos()

    print("")
    print("cursor end   : (%d, %d)" % cursor_end)
    print("cursor range : x %d..%d   y %d..%d" %
          (cursor_min[0], cursor_max[0], cursor_min[1], cursor_max[1]))
    print("board MOVE   : x=%d y=%d" % last_move)
    print("host samples : %d" % samples)
    print("left clicks  : %d edge(s) seen by the host" % click_edges)
    if last_pad:
        print("board PAD    : %s" % last_pad)

    pad_taps = None
    for tok in last_pad.split():
        if tok.startswith("taps="):
            try:
                pad_taps = int(tok[5:])
            except ValueError:
                pass

    moved_span = (cursor_max[0] - cursor_min[0]) + (cursor_max[1] - cursor_min[1])
    ok = True

    if last_move != (0, 0) and moved_span >= 5:
        print("  move : PASS - the board reports movement and the host cursor moved")
    elif last_move != (0, 0):
        print("  move : FAIL - the board reports movement but the host cursor did not move")
        ok = False
    else:
        print("  move : SKIP - no movement reported by the board")
        ok = False

    if click_edges > 0:
        print("  click: PASS - the host saw %d left-button press(es)" % click_edges)
    elif pad_taps:
        print("  click: FAIL - the board emitted %d tap click(s) but the host saw none"
              % pad_taps)
        ok = False
    else:
        print("  click: SKIP - no tap registered by the board either")
        ok = False

    print("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
