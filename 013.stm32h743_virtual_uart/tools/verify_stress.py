#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
PC-side acceptance for Phase 7 of the STM32H743 software UART.

Runs the whole stress programme against the echo firmware and reports one
pass/fail tally:

  T1  large transfer   100000 bytes round-trip, byte-exact
  T2  boundary sizes   1 .. 4097 bytes, each byte-exact, so every FIFO edge
                       (empty, one slot, one past the ring, wrap) is covered
  T3  soak             continuous full-duplex echo for --soak seconds
  T4  framing errors   a burst sent at the wrong baud must be flagged by the
                       receiver and the link must recover cleanly afterwards

The on-chip counters are read over SWD between phases as corroboration.

Usage:
    python verify_stress.py                       # all of it
    python verify_stress.py --soak 1800           # 30-minute soak
    python verify_stress.py --skip-soak --skip-large
"""

import argparse
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("ERROR: pyserial missing. Install into the system Python:")
    print("  D:/Software/Python3/python.exe -m pip install pyserial")
    sys.exit(2)

ROOT = Path(__file__).resolve().parent.parent
OOCD_CFG = ROOT / "openocd" / "stm32h743_stlink.cfg"
DEFAULT_ELF = ROOT / "build" / "debug" / "h743_virtual_uart.elf"

PROBE_MAGIC = 0x45434854
PROBE_NAMES = ["magic", "echoed", "rx_bytes", "tx_bytes",
               "rx_overflow", "rx_frame_error", "tx_overflow", "loops"]

BOUNDARIES = [1, 2, 3, 63, 64, 65, 127, 128, 255, 256, 1023, 1024, 1025,
              2047, 2048, 4095, 4096, 4097]


def parse_args():
    ap = argparse.ArgumentParser(description="software UART Phase 7 stress")
    ap.add_argument("--port", default="COM19")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--large", type=int, default=100000)
    ap.add_argument("--soak", type=float, default=600.0,
                    help="seconds of continuous echo (0 disables)")
    ap.add_argument("--elf", default=str(DEFAULT_ELF))
    ap.add_argument("--no-probe", action="store_true")
    ap.add_argument("--skip-large", action="store_true")
    ap.add_argument("--skip-soak", action="store_true")
    ap.add_argument("--skip-boundary", action="store_true")
    ap.add_argument("--skip-framing", action="store_true")
    return ap.parse_args()


# --------------------------------------------------------------------- utils
def make_pattern(n, seed=0x12345678):
    out = bytearray()
    x = seed
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


def reset_target():
    try:
        subprocess.run(["openocd", "-f", str(OOCD_CFG),
                        "-c", "init", "-c", "reset run", "-c", "exit"],
                       capture_output=True, timeout=20, check=True)
    except Exception as exc:                       # noqa: BLE001
        print(f"FAIL  openocd reset: {exc}")
        return False
    return True


def probe_address(elf):
    try:
        out = subprocess.run(["arm-none-eabi-nm", elf],
                             capture_output=True, text=True, timeout=20).stdout
    except Exception:                              # noqa: BLE001
        return None
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "g_echo":
            return int(parts[0], 16)
    return None


def read_probe(addr):
    cmd = ["openocd", "-f", str(OOCD_CFG),
           "-c", "init", "-c", "halt",
           "-c", "mdw 0x%08X %d" % (addr, len(PROBE_NAMES)),
           "-c", "resume", "-c", "exit"]
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=40)
    text = (res.stdout or "") + "\n" + (res.stderr or "")
    vals = []
    for line in text.splitlines():
        if ":" not in line or not line.lstrip().startswith("0x"):
            continue
        for tok in line.split(":", 1)[1].split():
            try:
                vals.append(int(tok, 16))
            except ValueError:
                pass
    return dict(zip(PROBE_NAMES, vals))


class Echo():
    """Full-duplex session: a reader thread keeps draining the port."""

    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.lock = threading.Lock()
        self.buf = bytearray()
        self.stop = threading.Event()
        self.th = threading.Thread(target=self._reader, daemon=True)
        self.th.start()

    def _reader(self):
        while not self.stop.is_set():
            try:
                chunk = self.ser.read(4096)
            except Exception:                      # noqa: BLE001
                break
            if chunk:
                with self.lock:
                    self.buf.extend(chunk)

    def mark(self):
        with self.lock:
            return len(self.buf)

    def since(self, mark):
        with self.lock:
            return bytes(self.buf[mark:])

    def run(self, pattern, chunk=512, timeout=180.0):
        """Send `pattern`, wait until its echo has come back."""
        mark = self.mark()
        off = 0
        while off < len(pattern):
            n = min(chunk, len(pattern) - off)
            self.ser.write(pattern[off:off + n])
            off += n
        self.ser.flush()

        deadline = time.time() + timeout
        while time.time() < deadline:
            if len(self.buf) - mark >= len(pattern):
                break
            time.sleep(0.02)
        time.sleep(0.3)                # let a straggler land
        return self.since(mark)

    def drain(self):
        """Throw away whatever is in flight and start clean."""
        time.sleep(0.3)
        self.mark()
        with self.lock:
            self.buf = bytearray()
            return 0

    def close(self):
        self.stop.set()
        self.th.join(timeout=2.0)
        self.ser.close()


def compare(echo, pattern):
    """(mismatches, first_index, length_ok)"""
    n = min(len(echo), len(pattern))
    bad = 0
    first = -1
    for i in range(n):
        if echo[i] != pattern[i]:
            bad += 1
            if first < 0:
                first = i
    return bad, first, (len(echo) == len(pattern))


# --------------------------------------------------------------------- main
def main():
    args = parse_args()
    results = []

    def record(name, ok, detail):
        results.append((name, ok, detail))
        print(f"  {'PASS' if ok else 'FAIL'}  {name}: {detail}")

    if not reset_target():
        return 2

    try:
        echo = Echo(args.port, args.baud)
    except Exception as exc:                       # noqa: BLE001
        print(f"FAIL  open {args.port}: {exc}")
        return 2

    total_sent = 0
    baud_bits = args.baud / 10.0

    # ---------------------------------------------------------------- T1
    if not args.skip_large:
        print(f"\n[T1] large transfer: {args.large} bytes round-trip")
        pat = make_pattern(args.large)
        t0 = time.time()
        got = echo.run(pat, timeout=args.large / baud_bits + 60.0)
        dt = time.time() - t0
        bad, first, lenok = compare(got, pat)
        rate = len(got) / dt if dt else 0.0
        record("T1 large transfer", bad == 0 and lenok,
               f"{len(got)}/{args.large} bytes, {bad} mismatches, "
               f"{dt:.1f}s, {rate:.0f} B/s")
        total_sent += args.large

    # ---------------------------------------------------------------- T2
    if not args.skip_boundary:
        print("\n[T2] boundary sizes")
        for n in BOUNDARIES:
            pat = make_pattern(n, seed=0xC0FFEE + n)
            got = echo.run(pat, timeout=n / baud_bits + 20.0)
            bad, first, lenok = compare(got, pat)
            detail = (f"{n} B -> {len(got)} B, {bad} mismatches"
                      + ("" if lenok else f", length differs"))
            record(f"T2 boundary {n}", bad == 0 and lenok, detail)
            total_sent += n

    # ---------------------------------------------------------------- T3
    if args.soak and not args.skip_soak:
        n = int(baud_bits * args.soak)
        print(f"\n[T3] soak: {args.soak:.0f}s continuous echo (~{n} bytes)")
        pat = make_pattern(n, seed=0x5EA5EA)
        t0 = time.time()
        got = echo.run(pat, timeout=args.soak + 120.0)
        dt = time.time() - t0
        bad, first, lenok = compare(got, pat)
        detail = (f"{len(got)}/{n} bytes over {dt:.1f}s, {bad} mismatches, "
                  f"{len(got)/dt:.0f} B/s")
        record("T3 soak", bad == 0 and lenok, detail)
        total_sent += n

    # ------------------------------------------------ probe (clean so far)
    if not args.no_probe:
        addr = probe_address(args.elf)
        if addr:
            p = read_probe(addr)
            ok = (p.get("magic") == PROBE_MAGIC and p.get("rx_overflow") == 0
                  and p.get("rx_frame_error") == 0
                  and p.get("echoed") == total_sent)
            record("probe clean phase", ok,
                   f"echoed={p.get('echoed')} (sent {total_sent}) "
                   f"rx_bytes={p.get('rx_bytes')} "
                   f"rx_overflow={p.get('rx_overflow')} "
                   f"rx_frame_error={p.get('rx_frame_error')} "
                   f"tx_overflow={p.get('tx_overflow')}")

    # ---------------------------------------------------------------- T4
    if not args.skip_framing:
        print("\n[T4] framing-error injection and recovery")
        echo.drain()
        wrong = int(args.baud / 2)
        echo.ser.baudrate = wrong
        echo.ser.write(make_pattern(64, seed=0xBAD))
        echo.ser.flush()
        time.sleep(0.3)
        echo.ser.baudrate = args.baud
        time.sleep(0.2)
        echo.drain()

        pat = make_pattern(2048, seed=0x600D)
        got = echo.run(pat, timeout=20.0)
        bad, first, lenok = compare(got, pat)
        record("T4 recovery after bad baud", bad == 0 and lenok,
               f"{len(got)}/{len(pat)} bytes, {bad} mismatches")

    echo.close()

    # ------------------------------------------------------- final probe
    if not args.no_probe:
        addr = probe_address(args.elf)
        if addr:
            p = read_probe(addr)
            if not args.skip_framing:
                ok = p.get("rx_frame_error", 0) > 0
                record("T4 detected the bad frames", ok,
                       f"rx_frame_error={p.get('rx_frame_error')}")
            record("probe final", p.get("rx_overflow") == 0
                   and p.get("tx_overflow") == 0,
                   f"rx_overflow={p.get('rx_overflow')} "
                   f"tx_overflow={p.get('tx_overflow')} "
                   f"echoed={p.get('echoed')} loops={p.get('loops')}")

    passed = sum(1 for _, ok, _ in results if ok)
    failed = len(results) - passed
    print(f"\nTOTAL: {passed} passed, {failed} failed "
          f"({len(results)} checks)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
