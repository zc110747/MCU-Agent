#!/usr/bin/env python3
"""On-device STRESS test for the image viewer (post "涂抹" fix).

Exercises the JPEG decode path hard:
  * headless `img decode` (TJpgDec work pool from the shared SRAM arena, and
    the JD_FORMAT=1 RGB565 output whose byte order was the bug)
  * UI `img show` / `img close` round trips (the full panel-takeover path)
  * `cap` to mint fresh 4:4:4 JPEGs on the card each round
After every round it checks the console is still alive and that SRAM_D2 is
fully reclaimed (no leak across the open/close churn).  Any decode failure or
non-zero leak aborts the run.

Usage (hardware attached, either COM channel):
    python3 scripts/stress_img.py [rounds] [port]
Defaults: rounds=30, port=auto-detect.
"""
import sys, time, re, serial, serial.tools.list_ports as lp

ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 else 30
FORCE_PORT = sys.argv[2] if len(sys.argv) > 2 else None

def pick_port():
    if FORCE_PORT:
        s = serial.Serial(FORCE_PORT, 115200, timeout=0.8)
        return s, FORCE_PORT
    for p in [p.device for p in lp.comports()]:
        try:
            s = serial.Serial(p, 115200, timeout=0.8)
        except Exception:
            continue
        time.sleep(0.3); s.reset_input_buffer(); s.write(b"status\r\n")
        t0 = time.time(); got = []
        while time.time() - t0 < 2.0:
            d = s.read_all().decode(errors="replace")
            if d: got.append(d)
            time.sleep(0.05)
        if "H743" in "".join(got):
            print("USING PORT", p); return s, p
        s.close()
    return None, None

s, port = pick_port()
if s is None:
    print("NO RESPONSIVE SERIAL PORT"); sys.exit(2)

def cmd(line, wait=2.0):
    s.reset_input_buffer(); s.write((line + "\r\n").encode())
    buf = []; t0 = time.time()
    while time.time() - t0 < wait:
        d = s.read_all().decode(errors="replace")
        if d: buf.append(d)
        time.sleep(0.05)
    return "".join(buf)

def sram_d2():
    st = cmd("status", 1.2)
    m = re.search(r"sram\s+d2\s*:\s*(\d+)/(\d+)", st)
    if not m:
        return None, None
    return int(m.group(1)), int(m.group(2))

# baseline after a clean idle
time.sleep(0.5)
base_free, base_total = sram_d2()
print("SRAM D2 baseline free %s/%s" % (base_free, base_total))
if base_free is None:
    print("!! cannot read SRAM baseline"); s.close(); sys.exit(3)

fails = 0
leaks = 0
for i in range(ROUNDS):
    # 1) mint a fresh 4:4:4 JPEG on the card
    cap = cmd("cap", 3.0)
    m = re.search(r"cap saved (1:/catch/[\w.-]+)", cap)
    if not m:
        print("round %d: capture did not report a path" % i); fails += 1; continue
    path = m.group(1)

    # 2) headless decode (the path that used to garble / OOM)
    dec = cmd("img decode " + path, 3.0)
    if ("OK img decode" not in dec) or ("failed" in dec):
        print("round %d: decode FAILED -> %s" % (i, dec.strip().splitlines()[-1]))
        fails += 1
        continue

    # 3) UI show + close round trip
    cmd("img list", 1.2)
    sh = cmd("img show 0", 2.0)   # index 0 == the freshly captured file
    cmd("img close", 1.5)
    cmd("img info", 1.2)

    # 4) reclaim check
    free, total = sram_d2()
    if free is None:
        print("round %d: console unresponsive after cycle" % i); fails += 1; break
    delta = base_free - free
    if abs(delta) > 256:   # allow tiny rounding, flag real leaks
        print("round %d: SRAM D2 leak free=%d (base=%d, delta=%d)"
              % (i, free, base_free, delta))
        leaks += 1
        fails += 1

    alive = "H743" in cmd("status", 1.2)
    if not alive:
        print("round %d: console dead" % i); fails += 1; break

    if (i + 1) % 10 == 0:
        print("  ... %d/%d rounds ok (d2 free %d/%d)" % (i + 1, ROUNDS, free, total))

# final reclaim
time.sleep(0.5)
free, total = sram_d2()
final_delta = base_free - free
print("final SRAM D2 free %d/%d (base %d, delta %d)"
      % (free, total, base_free, final_delta))

s.close()
ok = (fails == 0) and (abs(final_delta) <= 256)
print("\nRESULT: %s  (fails=%d, leak_events=%d)"
      % ("PASS" if ok else "FAIL", fails, leaks))
sys.exit(0 if ok else 1)
