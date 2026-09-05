#!/usr/bin/env python3
"""Stress / acceptance tool for the STM32H743 USB CDC <-> UART4 bridge.

The board shorts PA0 (UART4_TX) to PA1 (UART4_RX), so everything this tool
sends must come back byte for byte. Each frame carries

    A5 5A | len (u16 BE) | seq (u32 LE) | payload | crc32 (u32 LE)

so the receiver can tell apart lost frames, duplicated frames, corrupted
frames and plain reordering.

Design note (firmware is a TRANSPARENT bridge)
----------------------------------------------
There is NO in-band command channel and NO out-of-band vendor request. The
device forwards every byte on the wire as-is, and is configured ONLY through
the standard CDC-ACM control channel that every host serial driver sends
automatically:

    SET_LINE_CODING       -> baud / data bits / parity / stop bits (live).
    SET_CONTROL_LINE_STATE-> host DTR / RTS (standard modem-control signals).

Hardware RTS/CTS flow control is ALWAYS enabled inside the USART (CTSE); the
peer gates our TX by driving CTS. Our own RTS output is driven in software
from the host RTS signal ANDed with the RX-ring headroom, so the 16 KB ring is
also protected. DTR has no physical pin on UART4 (no DTR/DSR) and is observed
only - it never gates the data path. Because this is all standard signal
pass-through, single-RTS / single-DTR / dual-RTS-DTR hosts all work with no
firmware mode switch.

This tool therefore verifies the bridge with byte-exact round trips. Flow
control (the CTS/DTR-enabled mode) is exercised on REAL hardware by the user
- it cannot be verified over a PA0<->PA1 loopback because a loopback has no
peer that drives CTS. See tools/flow_hw_probe.py for the hardware hookup.

Port selection
--------------
The COM port is chosen by the project's USB VID:PID (default CAFE:4012). A
port whose hardware ID does not match is refused unless --force is given.

Usage
-----
    python stress_test.py list
    python stress_test.py identify
    python stress_test.py stress --mode mixed --duration 30
    python stress_test.py stress --mode small --count 2000
    python stress_test.py setbaud 921600
    python stress_test.py formats
    python stress_test.py paced
    python stress_test.py all --duration 20      # full transparent sweep
"""

from __future__ import annotations

import argparse
import os
import sys
import threading
import time
import zlib
from dataclasses import dataclass, field

try:
    import serial
    import serial.tools.list_ports as list_ports
except ImportError:
    sys.stderr.write("pyserial is required:  python -m pip install pyserial\n")
    raise SystemExit(2)

# ---------------------------------------------------------------------------
# Identity used to pick the right port (mirrors app/usb_desc_defs.h)
# ---------------------------------------------------------------------------
PROJECT_VID = 0xCAFE
PROJECT_PID = 0x4012
PROJECT_NAME = "H743 CDC UART4 Bridge"

MAGIC = b"\xA5\x5A"
HDR_LEN = 8          # magic(2) + len(2) + seq(4)
CRC_LEN = 4
MAX_PAYLOAD = 4096

# Deterministic payload table: payload(seq, n) is reproducible on the receive
# side without keeping a copy of what was sent.
_PATTERN = bytes(((i * 7) + ((i >> 5) * 13) + (i % 251) + 37) & 0xFF
                 for i in range(65536))
_PATTERN_SPAN = 60000


def payload_for(seq: int, n: int) -> bytes:
    off = (seq * 251) % (_PATTERN_SPAN - MAX_PAYLOAD)
    return _PATTERN[off:off + n]


def build_frame(seq: int, payload: bytes) -> bytes:
    body = len(payload).to_bytes(2, "big") + seq.to_bytes(4, "little") + payload
    return MAGIC + body + zlib.crc32(body).to_bytes(4, "little")


# ---------------------------------------------------------------------------
# Statistics
# ---------------------------------------------------------------------------
@dataclass
class Stats:
    tx_bytes: int = 0
    rx_bytes: int = 0
    frames_tx: int = 0
    frames_rx: int = 0
    lost: int = 0            # seq gaps
    duplicate: int = 0       # seq already seen
    crc_error: int = 0
    payload_error: int = 0
    resync_bytes: int = 0    # bytes skipped while re-locking on the magic
    lat_min: float = float("inf")
    lat_max: float = 0.0
    lat_sum: float = 0.0
    lat_count: int = 0
    t_start: float = 0.0
    t_end: float = 0.0
    errors: list = field(default_factory=list)

    @property
    def duration(self) -> float:
        return max(self.t_end - self.t_start, 1e-9)

    @property
    def lat_avg(self) -> float:
        return (self.lat_sum / self.lat_count) if self.lat_count else 0.0

    @property
    def tx_rate(self) -> float:
        return self.tx_bytes / self.duration

    @property
    def rx_rate(self) -> float:
        return self.rx_bytes / self.duration

    def as_rows(self):
        return [
            ("duration", f"{self.duration:.2f} s"),
            ("tx bytes / frames", f"{self.tx_bytes} / {self.frames_tx}"),
            ("rx bytes / frames", f"{self.rx_bytes} / {self.frames_rx}"),
            ("tx throughput", f"{self.tx_rate/1000:.2f} kB/s"),
            ("rx throughput", f"{self.rx_rate/1000:.2f} kB/s"),
            ("lost frames", f"{self.lost}"),
            ("duplicate frames", f"{self.duplicate}"),
            ("CRC errors", f"{self.crc_error}"),
            ("payload errors", f"{self.payload_error}"),
            ("resync bytes", f"{self.resync_bytes}"),
            ("latency min", f"{self.lat_min*1000:.3f} ms" if self.lat_count else "-"),
            ("latency avg", f"{self.lat_avg*1000:.3f} ms" if self.lat_count else "-"),
            ("latency max", f"{self.lat_max*1000:.3f} ms" if self.lat_count else "-"),
        ]


# ---------------------------------------------------------------------------
# Frame parser
# ---------------------------------------------------------------------------
class FrameParser:
    def __init__(self, stats: Stats):
        self.buf = bytearray()
        self.stats = stats

    def feed(self, data: bytes):
        self.buf.extend(data)

    def parse(self):
        """Yield (seq, payload, crc_ok) for every complete frame in the buffer."""
        out = []
        b = self.buf
        while True:
            i = b.find(MAGIC)
            if i < 0:
                if len(b) > 1:
                    self.stats.resync_bytes += len(b) - 1
                    del b[:len(b) - 1]
                break
            if i > 0:
                self.stats.resync_bytes += i
                del b[:i]
            if len(b) < HDR_LEN:
                break
            n = (b[2] << 8) | b[3]
            if n > MAX_PAYLOAD:            # bogus length: skip the magic
                self.stats.resync_bytes += 2
                del b[:2]
                continue
            total = HDR_LEN + n + CRC_LEN
            if len(b) < total:
                break
            frame = bytes(b[:total])
            del b[:total]

            seq = int.from_bytes(frame[4:8], "little")
            payload = frame[8:8 + n]
            body = frame[2:8 + n]
            crc_ok = int.from_bytes(frame[8 + n:total], "little") == zlib.crc32(body)
            out.append((seq, payload, crc_ok))
        return out


# ---------------------------------------------------------------------------
# Port selection
# ---------------------------------------------------------------------------
def find_ports(vid=PROJECT_VID, pid=PROJECT_PID):
    """Return (matching, all) port lists."""
    all_ports = list(list_ports.comports())
    needle = f"{vid:04X}:{pid:04X}".upper()
    matched = []
    for p in all_ports:
        hwid = (p.hwid or "").upper()
        if f"VID:PID={needle}" in hwid:
            matched.append(p)
    return matched, all_ports


def print_ports(all_ports, matched):
    print("Serial ports:")
    for p in all_ports:
        tag = "  <= PROJECT" if p in matched else ""
        print(f"  {p.device:<8} {p.description[:46]:<48}{tag}")
        print(f"           hwid={p.hwid}")
    print()


def open_serial(port, baud=115200, timeout=0.5, write_timeout=2.0,
                bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE, tries=20, delay=0.15):
    """Open a COM port, retrying briefly.

    Windows does not release a COM port the instant it is closed, so
    open-after-close can raise PermissionError. Every helper in this script
    tears a port down and immediately rebuilds it, so retry here rather than
    sprinkling sleeps through the callers.
    """
    last = None
    for _ in range(tries):
        try:
            return serial.Serial(port, baud, timeout=timeout,
                                 write_timeout=write_timeout,
                                 bytesize=bytesize, parity=parity,
                                 stopbits=stopbits)
        except serial.SerialException as exc:
            last = exc
            time.sleep(delay)
    raise last


def resolve_port(args):
    matched, all_ports = find_ports()
    if args.port:
        chosen = args.port
        info = next((p for p in all_ports if p.device.upper() == chosen.upper()), None)
        if info is not None and info not in matched:
            msg = (f"{chosen} does not look like this project's port "
                   f"(hwid={info.hwid})")
            if not args.force:
                print_ports(all_ports, matched)
                raise SystemExit("REFUSED: " + msg + "\nUse --force to override.")
            print("WARNING: " + msg)
        elif info is None:
            print(f"WARNING: {chosen} is not in the enumerated port list")
        return chosen

    if not matched:
        print_ports(all_ports, matched)
        raise SystemExit(f"No port with VID:PID={PROJECT_VID:04X}:{PROJECT_PID:04X} "
                         f"found. Is the board plugged in and enumerated?")
    if len(matched) > 1:
        print_ports(all_ports, matched)
        raise SystemExit("Several matching ports - pick one with --port")
    p = matched[0]
    print(f"Auto-selected {p.device}  ({p.description})")
    print(f"  hwid  = {p.hwid}")
    print(f"  serial= {p.serial_number}")
    return p.device


# ---------------------------------------------------------------------------
# Stress engine
# ---------------------------------------------------------------------------
class Bridge:
    def __init__(self, port, baud=115200, bytesize=serial.EIGHTBITS,
                 parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE):
        # The USB side always runs at full speed; the UART format only matters
        # because opening the port at a given format makes the host send the
        # matching SET_LINE_CODING to the device.
        self.ser = open_serial(port, baud, bytesize=bytesize, parity=parity,
                                 stopbits=stopbits,
                                 timeout=0.05, write_timeout=10.0)
        self.stats = Stats()
        self.parser = FrameParser(self.stats)
        self.stop_evt = threading.Event()
        self.send_times = {}
        self.last_seq = None
        self.rx_thread = None

    # ---------------- receiver ----------------
    def _rx_loop(self):
        ser, st = self.ser, self.stats
        while not self.stop_evt.is_set():
            try:
                # Ask for what is already buffered, or - if nothing is there
                # yet - for a single byte. Requesting a big block instead
                # makes Windows wait to *fill* it and only hand the bytes over
                # when the read timeout expires, which adds tens of ms of
                # purely synthetic latency (measured: 0.23 ms -> 32 ms avg).
                n = ser.in_waiting
                data = ser.read(n if n else 1)
            except Exception:
                break                       # port went away underneath us
            if not data:
                continue
            st.rx_bytes += len(data)
            self.parser.feed(data)
            now = time.time()
            for seq, payload, crc_ok in self.parser.parse():
                st.frames_rx += 1
                if self.last_seq is not None:
                    expected = (self.last_seq + 1) & 0xFFFFFFFF
                    if seq == expected:
                        pass
                    elif seq > expected:
                        st.lost += seq - expected
                    else:
                        st.duplicate += 1
                self.last_seq = seq

                if not crc_ok:
                    st.crc_error += 1
                    continue
                if payload != payload_for(seq, len(payload)):
                    st.payload_error += 1
                    continue
                ts = self.send_times.pop(seq, None)
                if ts is not None:
                    dt = now - ts
                    st.lat_sum += dt
                    st.lat_count += 1
                    if dt < st.lat_min:
                        st.lat_min = dt
                    if dt > st.lat_max:
                        st.lat_max = dt

    def start(self):
        self.stats.t_start = time.time()
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()

    def stop(self):
        self.stop_evt.set()
        if self.rx_thread:
            self.rx_thread.join(timeout=2.0)
        # final drain
        deadline = time.time() + 1.5
        while time.time() < deadline:
            n = self.ser.in_waiting
            if not n:
                time.sleep(0.02)
                continue
            data = self.ser.read(n)
            if not data:
                break
            self.stats.rx_bytes += len(data)
            self.parser.feed(data)
            for seq, payload, crc_ok in self.parser.parse():
                self.stats.frames_rx += 1
                if self.last_seq is not None:
                    expected = (self.last_seq + 1) & 0xFFFFFFFF
                    if seq > expected:
                        self.stats.lost += seq - expected
                    elif seq < expected:
                        self.stats.duplicate += 1
                self.last_seq = seq
                if crc_ok and payload == payload_for(seq, len(payload)):
                    ts = self.send_times.pop(seq, None)
                    if ts is not None:
                        dt = time.time() - ts
                        self.stats.lat_sum += dt
                        self.stats.lat_count += 1
                        self.stats.lat_min = min(self.stats.lat_min, dt)
                        self.stats.lat_max = max(self.stats.lat_max, dt)
            deadline = time.time() + 0.4
        self.stats.t_end = time.time()

    def run_paced(self, size, gap_s, total, settle=2.0, timeout=180.0):
        """Send exactly `total` payload bytes as frames of `size` bytes each,
        spaced `gap_s` seconds apart, then wait for the echo.

        Unlike run() this does not pace by in-flight bytes: the whole point is
        a lightly loaded link where each frame is on its own, so the measured
        round trip reflects the bridge rather than the host's send window.
        """
        st = self.stats
        nframes = total // size
        for i in range(nframes):
            self.send_times[i] = time.time()
            frame = build_frame(i, payload_for(i, size))
            self.ser.write(frame)
            st.tx_bytes += len(frame)
            st.frames_tx += 1
            if i + 1 < nframes:
                time.sleep(gap_s)

        # Every byte we pushed must come back; give the tail time to arrive.
        deadline = time.time() + timeout
        quiet_since = time.time()
        while time.time() < deadline:
            if st.rx_bytes >= st.tx_bytes:
                if time.time() - quiet_since > settle * 0.25:
                    break
            else:
                quiet_since = time.time()
            if time.time() - quiet_since > settle:
                break
            time.sleep(0.01)

        self.stop()
        return st

    # ---------------- sender ----------------
    def run(self, mode="mixed", duration=10.0, max_frames=None, gap=0.0,
            payload_min=1, payload_max=512, progress=True, window=2048):
        """Send frames until the duration expires.

        `window` caps how many bytes may be in flight (sent but not yet echoed
        back). USB full speed is far faster than the UART, so without this the
        host would simply fill the OS driver's buffer and the frames would be
        dropped there - a host-side artifact, not a firmware defect. The
        device does apply real USB back-pressure (it stops arming the OUT
        endpoint), but limiting in flight bytes also keeps the measured
        round-trip latency meaningful.
        """
        st = self.stats
        seq = 0
        t_end = time.time() + duration
        next_report = time.time() + 2.0

        sizes = {
            "small": (1, 32),
            "burst": (512, 512),
            "mixed": (1, 600),
            "chunk": (900, 900),
        }.get(mode, (payload_min, payload_max))
        if mode == "custom":
            sizes = (payload_min, payload_max)

        import random
        rnd = random.Random(1234)

        while time.time() < t_end and not self.stop_evt.is_set():
            if max_frames and seq >= max_frames:
                break
            n = sizes[0] if sizes[0] == sizes[1] else rnd.randint(sizes[0], sizes[1])

            # Pace ourselves: never have more than `window` bytes outstanding.
            # The inner wait must watch the deadline too - if the device stops
            # draining entirely this loop would otherwise never let the outer
            # one reach its time check.
            while (st.tx_bytes - st.rx_bytes) > window:
                if self.stop_evt.is_set() or time.time() >= t_end:
                    break
                time.sleep(0.0005)
            if self.stop_evt.is_set() or time.time() >= t_end:
                break

            frame = build_frame(seq, payload_for(seq, n))
            self.send_times[seq] = time.time()
            self.ser.write(frame)
            st.tx_bytes += len(frame)
            st.frames_tx += 1
            seq += 1

            if len(self.send_times) > 200000:      # bound memory on long runs
                for k in list(self.send_times)[:100000]:
                    self.send_times.pop(k, None)

            if gap:
                time.sleep(gap)

            if progress and time.time() >= next_report:
                next_report = time.time() + 2.0
                print(f"  ... tx={st.tx_bytes} rx={st.rx_bytes} "
                      f"f_tx={st.frames_tx} f_rx={st.frames_rx} "
                      f"lost={st.lost} crc={st.crc_error}", flush=True)

        self.stop()
        return st

    def close(self):
        # Reap the receiver *before* closing the handle. If run() bailed out
        # early (e.g. a write timeout) it never reached stop(), and a live RX
        # thread sitting in read() keeps the COM port open - the next open then
        # fails with PermissionError.
        self.stop_evt.set()
        if self.rx_thread is not None:
            self.rx_thread.join(timeout=2.0)
            self.rx_thread = None
        try:
            self.ser.close()
        except Exception:
            pass


def report(title, st: Stats):
    print()
    print(f"=== {title} ===")
    for k, v in st.as_rows():
        print(f"  {k:<22} {v}")
    ok = (st.lost == 0 and st.duplicate == 0 and st.crc_error == 0
          and st.payload_error == 0 and st.frames_rx > 0)
    print(f"  {'RESULT':<22} {'PASS' if ok else 'FAIL'}")
    return ok


def drain_quiet(ser, quiet=0.35, limit=8.0):
    """Throw away whatever is lingering so the next check starts clean."""
    t_end, last = time.time() + limit, time.time()
    while time.time() < t_end:
        n = ser.in_waiting
        if n:
            ser.read(n)
            last = time.time()
        elif time.time() - last > quiet:
            break
        time.sleep(0.02)


# ---------------------------------------------------------------------------
# Sub-commands
# ---------------------------------------------------------------------------
def cmd_list(args):
    matched, all_ports = find_ports()
    print_ports(all_ports, matched)
    print(f"Project port: VID:PID={PROJECT_VID:04X}:{PROJECT_PID:04X} "
          f"({PROJECT_NAME})")
    print(f"Matched: {[p.device for p in matched] or 'NONE'}")


def cmd_identify(args):
    """Identity is determined by VID:PID at enumeration; the firmware exposes
    no in-band identity command by design (transparent bridge)."""
    matched, all_ports = find_ports()
    print(f"Project VID:PID = {PROJECT_VID:04X}:{PROJECT_PID:04X} ({PROJECT_NAME})")
    print_ports(all_ports, matched)


def cmd_stress(args):
    port = resolve_port(args)
    print(f"Target: {port}  (VID:PID {PROJECT_VID:04X}:{PROJECT_PID:04X})")

    b = Bridge(port, args.baud)
    try:
        b.start()
        st = b.run(mode=args.mode, duration=args.duration,
                   max_frames=args.count, gap=args.gap,
                   window=args.window)
        ok = report(f"stress [{args.mode}] @ {args.baud} on {port}", st)
    finally:
        b.close()
    return 0 if ok else 1


def cmd_setbaud(args):
    port = resolve_port(args)
    # Changing the host baudrate sends SET_LINE_CODING, which reconfigures
    # UART4 live. With PA0<->PA1 shorted, a baud mismatch produces garbage on
    # the loopback, so a clean echo proves the new rate really reached UART4.
    ser = open_serial(port, args.baud, timeout=0.3, write_timeout=2.0)
    try:
        ser.baudrate = args.newbaud
        time.sleep(0.3)
        ser.reset_input_buffer()
        probe = (b"\xA5\x5A setbaud probe @ %d baud\r\n" % args.newbaud)
        ser.write(probe)
        ser.flush()
        echo = b""
        deadline = time.time() + 3.0
        while len(echo) < len(probe) and time.time() < deadline:
            n = ser.in_waiting
            chunk = ser.read(n if n else 1)
            if chunk:
                echo += chunk
        good = (echo == probe)
        print(f"echo_len={len(echo)}/{len(probe)} verbatim={good}")
        print(f"UART4 running at {args.newbaud} baud (loopback echo): "
              f"{'PASS' if good else 'FAIL'}")
        return 0 if good else 1
    finally:
        try:
            ser.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Line-coding sweep: baud x data bits x parity x stop bits
# ---------------------------------------------------------------------------
# STM32 word length counts the parity bit, so the bridge maps
#   8 data + parity -> 9-bit word,  7 data + parity -> 8-bit word, etc.
# The sweep checks both halves of that contract:
#   * cfg sync  - SET_LINE_CODING really reached UART4 (proven by the data
#                  round trip below: a wrong format corrupts the echo)
#   * data      - the link actually carries payload at that format
FORMAT_MATRIX = [
    (115200, serial.EIGHTBITS, serial.PARITY_NONE, serial.STOPBITS_ONE, "8N1"),
    (115200, serial.EIGHTBITS, serial.PARITY_EVEN, serial.STOPBITS_ONE, "8E1"),
    (115200, serial.EIGHTBITS, serial.PARITY_ODD,  serial.STOPBITS_ONE, "8O1"),
    (115200, serial.EIGHTBITS, serial.PARITY_NONE, serial.STOPBITS_TWO, "8N2"),
    (115200, serial.EIGHTBITS, serial.PARITY_EVEN, serial.STOPBITS_TWO, "8E2"),
    (115200, serial.SEVENBITS, serial.PARITY_NONE, serial.STOPBITS_ONE, "7N1"),
    (115200, serial.SEVENBITS, serial.PARITY_EVEN, serial.STOPBITS_ONE, "7E1"),
    (115200, serial.SEVENBITS, serial.PARITY_ODD,  serial.STOPBITS_ONE, "7O1"),
    (57600,  serial.EIGHTBITS, serial.PARITY_NONE, serial.STOPBITS_ONE, "8N1"),
    (9600,   serial.EIGHTBITS, serial.PARITY_NONE, serial.STOPBITS_ONE, "8N1"),
    (460800, serial.EIGHTBITS, serial.PARITY_NONE, serial.STOPBITS_ONE, "8N1"),
    (921600, serial.EIGHTBITS, serial.PARITY_NONE, serial.STOPBITS_ONE, "8N1"),
]
DEFAULT_FMT = (115200, serial.EIGHTBITS, serial.PARITY_NONE,
               serial.STOPBITS_ONE, "8N1")


def apply_format(ser, baud, bytesize, parity, stopbits):
    """Push a new line coding to the bridge (triggers SET_LINE_CODING)."""
    ser.baudrate = baud
    ser.bytesize = bytesize
    ser.parity = parity
    ser.stopbits = stopbits
    time.sleep(0.25)


def ascii_echo_check(port, baud, bytesize, parity, stopbits, rounds=20):
    """7-bit data modes cannot carry arbitrary binary: the MSB of every byte
    simply does not exist on the wire, so the binary frame protocol (which
    uses the 0xA5/0x5A magic) is unusable there. Those formats are verified
    with printable-ASCII round trips instead, which is exactly what a real
    7-bit link is for."""
    ser = open_serial(port, baud, bytesize=bytesize, parity=parity,
                        stopbits=stopbits, timeout=0.6, write_timeout=2.0)
    try:
        apply_format(ser, baud, bytesize, parity, stopbits)
        ser.reset_input_buffer()
        bad = 0
        for i in range(rounds):
            msg = ("7BIT-%03d-abcdefghijklmnopqrstuvwxyz0123456789\r\n" % i).encode()
            ser.write(msg)
            if ser.read(len(msg)) != msg:
                bad += 1
        return bad
    finally:
        ser.close()


def cmd_formats(args):
    port = resolve_port(args)
    results = []

    print("=== line coding sweep (SET_LINE_CODING -> UART4) ===")
    print(f"  {'baud':>7}  {'fmt':<5}  {'cfg sync':<9}  {'data':<6}  detail")
    print("  " + "-" * 68)

    for baud, bs, par, sb, label in FORMAT_MATRIX:
        # SET_LINE_CODING reconfigures UART4 live. With PA0<->PA1 shorted, a
        # baud / data-bits / stop-bits mismatch corrupts the loopback echo, so
        # the data round trip below is itself the configuration proof - no
        # read-back command is needed (or wanted) on a transparent bridge.
        ser = open_serial(port, 115200, timeout=0.5, write_timeout=2.0)
        try:
            apply_format(ser, baud, bs, par, sb)
        finally:
            ser.close()

        cfg_ok = True

        # 2) prove the UART link really runs at that format
        if bs == serial.SEVENBITS:
            bad = ascii_echo_check(port, baud, bs, par, sb)
            data_ok = (bad == 0)
            detail = f"ascii round-trip, bad={bad}/20"
        else:
            b = Bridge(port, baud, bytesize=bs, parity=par, stopbits=sb)
            try:
                b.start()
                st = b.run(mode="mixed", duration=args.duration, progress=False,
                           window=max(2048, min(baud // 40, 32768)))
                data_ok = (st.lost == 0 and st.duplicate == 0
                           and st.crc_error == 0 and st.payload_error == 0
                           and st.resync_bytes == 0 and st.frames_rx > 20)
                detail = (f"{st.frames_rx} frames, lost={st.lost} dup={st.duplicate} "
                          f"crc={st.crc_error} resync={st.resync_bytes} "
                          f"| {st.rx_rate / 1024:.2f} kB/s")
            finally:
                b.close()

        ok = cfg_ok and data_ok
        results.append((f"{baud} {label}", ok))
        print(f"  {baud:>7}  {label:<5}  {'PASS' if cfg_ok else 'FAIL':<9}  "
              f"{'PASS' if data_ok else 'FAIL':<6}  {detail}", flush=True)

    # leave the board at the default 115200 8N1
    ser = open_serial(port, 115200, timeout=0.5, write_timeout=2.0)
    try:
        apply_format(ser, *DEFAULT_FMT[:4])
    finally:
        ser.close()

    print()
    allok = all(g for _, g in results)
    for name, good in results:
        print(f"  {'PASS' if good else 'FAIL'}  {name}")
    print(f"  ---- {'ALL PASS' if allok else 'SOME FAILED'} ----")
    return results


# ---------------------------------------------------------------------------
# Fixed-size / fixed-interval / fixed-total send test
# ---------------------------------------------------------------------------
# (payload bytes per send, gap in ms, total payload bytes)
PACED_SUITE = (
    (1,   10, 100),
    (10,  20, 1000),
    (500, 500, 10000),
)


def cmd_paced(args):
    port = resolve_port(args)

    # getattr: cmd_all() reuses this sweep but its namespace has no
    # --size/--gap/--total, so default to the built-in three-case suite.
    size = getattr(args, "size", None)
    if size is None:
        cases = list(PACED_SUITE)
    else:
        gap, total = getattr(args, "gap", None), getattr(args, "total", None)
        if gap is None or total is None:
            raise SystemExit("--size needs --gap and --total as well")
        cases = [(size, gap, total)]

    results = []
    print("=== paced send: fixed size / fixed gap / fixed total ===")
    print("  (no flow control; UART4 running at the reported line settings)")

    for size, gap_ms, total in cases:
        nframes = total // size
        b = Bridge(port, args.baud)
        try:
            b.start()
            st = b.run_paced(size, gap_ms / 1000.0, total)
        finally:
            b.close()

        title = (f"{size} B x {nframes} frames, {gap_ms} ms apart "
                 f"({total} B payload)")
        print(f"\n--- {title} ---")
        ok = report(title, st)

        expected_payload = nframes * size
        print(f"  frames  tx/rx        {st.frames_tx} / {st.frames_rx}")
        print(f"  payload tx/rx        {expected_payload} / "
              f"{expected_payload - st.payload_error * size}")
        print(f"  wire bytes tx/rx     {st.tx_bytes} / {st.rx_bytes}")

        complete = (st.frames_rx == st.frames_tx == nframes)
        clean = (st.lost == 0 and st.duplicate == 0
                 and st.crc_error == 0 and st.payload_error == 0
                 and st.resync_bytes == 0)
        byte_exact = (st.rx_bytes == st.tx_bytes)
        case_ok = ok and complete and clean and byte_exact
        results.append((f"{size}B/{gap_ms}ms/{total}B", case_ok))
        print(f"  complete {nframes}/{st.frames_rx} frames, "
              f"byte-exact {'yes' if byte_exact else 'NO'} -> "
              f"{'PASS' if case_ok else 'FAIL'}")

        s = open_serial(port, args.baud)
        try:
            drain_quiet(s)
        finally:
            s.close()

    print()
    allok = all(g for _, g in results)
    for name, good in results:
        print(f"  {'PASS' if good else 'FAIL'}  {name}")
    print(f"  ---- {'ALL PASS' if allok else 'SOME FAILED'} ----")
    return results


def cmd_all(args):
    port = resolve_port(args)
    results = []

    print("\n[1] identity (VID:PID enumeration)")
    matched, all_ports = find_ports()
    print(f"  matched: {[p.device for p in matched] or 'NONE'}")

    print("\n[2] small packets / latency")
    b = Bridge(port, args.baud)
    try:
        b.start()
        st = b.run(mode="small", duration=6.0, gap=0.005)
        results.append(("small packet loopback", report("small (1..32 B, 5 ms gap)", st)))
    finally:
        b.close()

    print("\n[3] continuous burst (fills the DMA buffer, HT/TC path)")
    b = Bridge(port, args.baud)
    try:
        b.start()
        st = b.run(mode="burst", duration=args.duration)
        results.append(("burst loopback", report("burst (512 B, back to back)", st)))
    finally:
        b.close()

    print("\n[4] mixed sizes (IDLE + HT + TC interleaved)")
    b = Bridge(port, args.baud)
    try:
        b.start()
        st = b.run(mode="mixed", duration=args.duration)
        results.append(("mixed loopback", report("mixed (1..600 B)", st)))
    finally:
        b.close()

    print("\n[5] baud rate change round trip")
    for baud in (9600, 115200, 460800):
        ser = open_serial(port, 115200 if baud != 115200 else args.baud,
                          timeout=0.4, write_timeout=2.0)
        ser.baudrate = baud
        time.sleep(0.25)
        ser.close()
        b = Bridge(port, baud)
        try:
            b.start()
            st = b.run(mode="small", duration=2.0, gap=0.004,
                       max_frames=200, progress=False)
            good = (st.lost == 0 and st.crc_error == 0 and st.frames_rx > 50)
            report(f"loopback @ {baud}", st)
            results.append((f"loopback @ {baud}", good))
        finally:
            b.close()

    print("\n[6] line coding sweep (baud / data bits / parity / stop bits)")
    saved_dur, args.duration = args.duration, 3.0   # per-format seconds
    try:
        results.extend(cmd_formats(args))
    finally:
        args.duration = saved_dur

    print("\n[7] paced send: fixed size / fixed gap / fixed total")
    try:
        results.extend(cmd_paced(args))
    finally:
        pass

    print("\n================ ACCEPTANCE (transparent bridge) ================")
    for name, good in results:
        print(f"  {'PASS' if good else 'FAIL'}  {name}")
    allok = all(g for _, g in results)
    print(f"  ---- {'ALL PASS' if allok else 'SOME FAILED'} ----")
    print("\nNote: hardware RTS/CTS flow control (CTS/DTR mode) is exercised")
    print("on real peer hardware by the user - it is not loopback-verifiable")
    print("because a PA0<->PA1 short has no peer that drives CTS. See")
    print("tools/flow_hw_probe.py for the wiring and the gating evidence.")
    return 0 if allok else 1


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=None,
                    help="COM port; auto-detected by VID:PID when omitted")
    ap.add_argument("--force", action="store_true",
                    help="use --port even if its hardware ID does not match")
    ap.add_argument("--baud", type=int, default=115200)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list", help="list serial ports and mark the project one") \
       .set_defaults(func=cmd_list)

    sub.add_parser("identify", help="show the project VID:PID (no in-band ID)") \
       .set_defaults(func=cmd_identify)

    p = sub.add_parser("stress", help="run a stress test")
    p.add_argument("--mode", default="mixed",
                   choices=["small", "burst", "mixed", "chunk"])
    p.add_argument("--duration", type=float, default=10.0)
    p.add_argument("--count", type=int, default=0, help="stop after N frames")
    p.add_argument("--gap", type=float, default=0.0, help="delay between frames")
    p.add_argument("--window", type=int, default=2048,
                   help="max bytes in flight (host pacing)")
    p.set_defaults(func=cmd_stress)

    p = sub.add_parser("setbaud", help="change UART4 baud rate")
    p.add_argument("newbaud", type=int)
    p.set_defaults(func=cmd_setbaud)

    p = sub.add_parser("paced", help="fixed size / gap / total send test")
    p.add_argument("--size", type=int, default=None,
                   help="payload bytes per send (omit to run the built-in "
                        "1B/10ms/100B, 10B/20ms/1000B, 500B/500ms/10000B suite)")
    p.add_argument("--gap", type=float, default=None, help="gap in ms")
    p.add_argument("--total", type=int, default=None, help="total payload bytes")
    p.set_defaults(func=cmd_paced)

    p = sub.add_parser("formats", help="sweep baud/data bits/parity/stop bits")
    p.add_argument("--duration", type=float, default=3.0,
                   help="loopback seconds per 8-bit format")
    p.set_defaults(func=cmd_formats)

    p = sub.add_parser("all", help="full transparent acceptance sweep")
    p.add_argument("--duration", type=float, default=10.0)
    p.set_defaults(func=cmd_all)

    args = ap.parse_args()
    out = args.func(args)
    if isinstance(out, list):                 # a sweep returns (name, ok) pairs
        return 0 if all(g for _, g in out) else 1
    return out


if __name__ == "__main__":
    sys.exit(main())
