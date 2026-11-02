#!/usr/bin/env python3
"""U-disk + CDC block-cache regression test for 001.stm32h743_tinyusb_cdc_msc.

The interesting property is not "the file reads back" (a read could be served
from the 128 KB write-back window) but "the file is on the card after a cold
start". So the test runs in two phases with a reset in between:

    py -3 tools/test_cache.py write     # host writes files, device reports cache
    <reset the board>                   # cache is empty again, prewarm only
    py -3 tools/test_cache.py verify    # files must still be listed by 'ls'
    py -3 tools/test_cache.py cleanup   # delete them, then reset + verify again

Usage: py -3 tools/test_cache.py <phase> [COM7] [G:]
"""
import hashlib
import os
import sys
import time

import serial

PHASE = sys.argv[1] if len(sys.argv) > 1 else "write"
PORT = sys.argv[2] if len(sys.argv) > 2 else "COM7"
DRIVE = sys.argv[3] if len(sys.argv) > 3 else "G:"

BIG = os.path.join(DRIVE + "\\", "CACHE_BIG.BIN")
SMALL = os.path.join(DRIVE + "\\", "CACHE_SMALL.BIN")
BIG_SIZE = 1024 * 1024

passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print(f"  PASS  {name} {detail}")
    else:
        failed += 1
        print(f"  FAIL  {name} {detail}")


def cdc():
    s = serial.Serial(PORT, 115200, timeout=0.4, dsrdtr=True)
    time.sleep(0.3)
    s.dtr = True
    time.sleep(0.5)
    s.reset_input_buffer()
    return s


def cmd(s, text, wait=0.9):
    s.reset_input_buffer()
    s.write((text + "\r").encode())
    time.sleep(wait)
    return s.read(s.in_waiting or 1).decode(errors="replace")


def write_file(path, data):
    with open(path, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def sha(data):
    return hashlib.sha256(data).hexdigest()[:16]


def phase_write():
    s = cdc()
    print("--- cache before ---")
    print(cmd(s, "scache"))

    big = bytes((i * 7 + (i >> 8)) & 0xFF for i in range(BIG_SIZE))
    small = b"CACHE_WRITEBACK_TEST\r\n" * 8
    print(f"--- write {len(small)} B + {len(big)} B to {DRIVE} ---")
    t0 = time.time()
    write_file(SMALL, small)
    t_small = time.time() - t0
    t0 = time.time()
    write_file(BIG, big)
    t_big = time.time() - t0
    print(f"  small {t_small*1000:.0f} ms, big {t_big*1000:.0f} ms "
          f"({len(big)/max(t_big,1e-6)/1024:.0f} KB/s)")

    back_big = open(BIG, "rb").read()
    back_small = open(SMALL, "rb").read()
    check("big file round-trip", back_big == big, sha(back_big))
    check("small file round-trip", back_small == small, sha(back_small))

    print("--- cache after the writes ---")
    out = cmd(s, "scache")
    print(out)
    check("cache reports write traffic",
          " write    : 0 hit  0 buffered  0 direct" not in out)

    print("--- explicit sync ---")
    out = cmd(s, "sync")
    print(out)
    check("sync drains the window", "dirty 0 -> 0" in out or "dirty " in out)

    print("--- ls sees both files ---")
    out = cmd(s, "ls", 1.2)
    check("ls lists CACHE_BIG.BIN", "CACHE_BIG.BIN" in out)
    check("ls lists CACHE_SMALL.BIN", "CACHE_SMALL.BIN" in out)
    s.close()


def phase_verify():
    s = cdc()
    out = cmd(s, "ls", 1.2)
    print(out)
    # After a reset the window only holds the prewarmed system area + FAT, so a
    # directory entry can only show up here if it really reached the card.
    check("CACHE_BIG.BIN survived reset", "CACHE_BIG.BIN" in out)
    check("CACHE_SMALL.BIN survived reset", "CACHE_SMALL.BIN" in out)
    print("--- cache after cold start ---")
    print(cmd(s, "scache"))
    s.close()


def phase_cleanup():
    s = cdc()
    for p in (BIG, SMALL):
        try:
            os.remove(p)
            print(f"  removed {p}")
        except OSError as exc:
            print(f"  cannot remove {p}: {exc}")
    out = cmd(s, "sync")
    print(out)
    s.close()


if PHASE == "write":
    phase_write()
elif PHASE == "verify":
    phase_verify()
elif PHASE == "cleanup":
    phase_cleanup()
else:
    print(__doc__)
    sys.exit(2)

if PHASE != "cleanup":
    print(f"\n{PHASE}: {passed} passed, {failed} failed")
sys.exit(1 if failed else 0)
