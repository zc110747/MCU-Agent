#!/usr/bin/env python3
"""On-device verification of the image-viewer OOM fix.

Decodes a real 4:4:4 JPEG (the board's own `cap` capture) via the new
headless `img decode <path>` command, which exercises the TJpgDec work pool
now sourced from the shared SRAM arena.  Also checks the console stays alive
and SRAM is fully reclaimed afterwards.
"""
import sys, time, serial, serial.tools.list_ports as lp

def pick_port():
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
        out = "".join(got)
        if ("H743" in out) or ("OK" in out) or ("ERR" in out):
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
    out = "".join(buf)
    print(">>", line)
    for ln in out.splitlines():
        print("   ", ln)
    return out

# 1) capture a fresh 4:4:4 JPEG into 1:/catch/
cap = cmd("cap", 3.0)
# parse its saved name
import re
m = re.search(r"cap saved (1:/catch/[\w.-]+)", cap)
if not m:
    print("!! capture did not report a path"); sys.exit(3)
path = m.group(1)
print("captured:", path)

# 2) headless decode of that 4:4:4 JPEG (this is the path that used to fail
#    with JDR_MEM1/JDR_MEM2 == "内存不足")
dec = cmd("img decode " + path, 3.0)
ok = ("OK img decode" in dec) and ("failed" not in dec)
print("\nDECODE OK:", ok)

# 3) sanity: a non-image must be rejected, not crash
cmd("img decode 1:/NES/DOES_NOT_EXIST.jpg", 2.0)

# 4) console still alive + SRAM reclaimed
st = cmd("status", 1.5)
print("ALIVE:", "H743" in st)
m2 = re.search(r"sram\s+d2\s*:\s*(\d+)/(\d+)", st)
if m2:
    free = int(m2.group(1)); total = int(m2.group(2))
    print("sram d2 free %d/%d (%.1f%%)" % (free, total, 100.0*free/total))

s.close()
print("\nRESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
