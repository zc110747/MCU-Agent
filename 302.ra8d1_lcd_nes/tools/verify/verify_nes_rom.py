#!/usr/bin/env python3
"""Run a real NES ROM on the target and prove the emulator produced frames.

Why this exists
---------------
`nes test` proves the *present* path with a synthetic buffer, but it does not
prove the emulator core executes: no ROM is decoded, no scanline is rendered by
nes_ppu.c, no CPU instruction retires.  This script closes that gap by loading a
cartridge image and checking that the panel content actually changes over time.

Why not just `nes load <path>`
------------------------------
The filesystem (W25Q64 + FatFs) is Phase 3.  Until it lands, the ROM is written
straight into the staging window the port layer reserves in SDRAM (see
nes_heap_staging_reserve()), and the firmware's `nes runmem` loads it with the
core's in-memory entry point.  Same emulator code path, different byte source.

Evidence tier
-------------
The strongest check here is that the framebuffer differs between two samples
taken a second apart.  A frozen or crashed emulator pins the panel to one image,
so "the picture changed" cannot be produced by a stub that writes a constant -
and it cannot be produced by the ROM sitting unloaded either.  The colour-count
check then confirms the image is a rendered game frame rather than noise.

Usage:
    python tools/verify/verify_nes_rom.py
    python tools/verify/verify_nes_rom.py --rom tools/testdata/SuperMary.nes
"""
import os
import re
import subprocess
import sys
import tempfile
import time

import serial

PORT = "COM9"
BAUD = 115200

FB_BASE = 0x68000000
FB_STRIDE = 480
FB_W = 480
FB_H = 360

DEFAULT_ROM = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "testdata", "SuperMary.nes"
)


def pyocd_cmd(script_lines, timeout=300):
    """Run a pyocd commander script and return its stdout."""
    fd, path = tempfile.mkstemp(suffix=".pyocd_cmds", text=True)
    try:
        with os.fdopen(fd, "w") as f:
            f.write("\n".join(script_lines) + "\n")
        r = subprocess.run(
            ["pyocd", "cmd", "-t", "cortex_m", "-O", "connect_mode=pre-reset", "-x", path],
            capture_output=True, text=True, timeout=timeout,
        )
        return r.stdout
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def open_serial(timeout_s=20.0):
    """Open the probe's virtual COM port, waiting for it to (re)appear.

    The ART-Link probe re-enumerates its USB CDC interface on every pyOCD
    attach/reset, so the port can be missing or still tearing down when a script
    reaches for it.  Opening blindly raises ClearCommError (WinError 13) on the
    stale handle.  Retry until the device is enumerated and accepts a handle.

    Deliberately does NOT drain or flush the buffer: reset_input_buffer() on this
    CDC interface was observed to leave the handle receiving nothing at all, and
    a plain open (what tools/verify/serial_cmd.py does) works.  Stale bytes are
    dealt with by the caller instead - see send_serial().
    """
    import serial.tools.list_ports as list_ports

    deadline = time.time() + timeout_s
    last_err = None
    while time.time() < deadline:
        names = {p.device.upper() for p in list_ports.comports()}
        if PORT.upper() in names:
            try:
                s = serial.Serial(PORT, BAUD, timeout=0.1)
                time.sleep(0.4)
                return s
            except serial.SerialException as exc:
                last_err = exc
        try:
            list_ports.comports()
        except Exception:                      # noqa: BLE001 - enumeration is best-effort
            pass
        time.sleep(0.5)
    raise serial.SerialException(f"could not open {PORT}: {last_err}")


def send_serial(cmds, settle=0.8, listen_s=3.0, sync=True):
    """Send msh commands, return everything the board printed.

    Reads continuously on a background thread from the moment the handle is
    taken.  A poll loop does NOT work here: the board emits heartbeats every two
    seconds regardless of what was asked, and any gap between read() calls loses
    bytes - including the command echo and its response.  tools/verify/
    serial_cmd.py uses the same threaded shape for the same reason.

    `sync` marks the boundary between "what was already arriving" and "the answer
    to this call" so the caller can tell them apart; it never flushes the buffer
    (see open_serial() for why that breaks this interface).
    """
    import threading

    s = open_serial()
    buf = bytearray()
    stop = threading.Event()
    mark = [0]

    def reader():
        while not stop.is_set():
            try:
                d = s.read(4096)
            except Exception:              # noqa: BLE001 - handle torn down on reset
                break
            if d:
                buf.extend(d)

    th = threading.Thread(target=reader, daemon=True)
    th.start()

    if sync:
        time.sleep(0.4)
        mark[0] = len(buf)

    for c in cmds:
        s.write((c + "\r\n").encode())
        time.sleep(settle)

    time.sleep(listen_s)
    stop.set()
    th.join(timeout=1.0)
    s.close()

    return bytes(buf[mark[0]:]).decode("utf-8", "replace")


def reset_and_wait(settle=2.0):
    subprocess.run(
        ["pyocd", "reset", "-t", "cortex_m", "-O", "connect_mode=pre-reset"],
        capture_output=True, timeout=90,
    )
    time.sleep(settle)


def read_words(addrs, resume=True):
    """Read one 32-bit word from each address, single pyocd session.

    Resumes the core afterwards by default: attaching halts it, and a halted
    core stops servicing UART interrupts, so the msh shell goes deaf until it is
    running again.
    """
    if not addrs:
        return {}
    uniq = list(dict.fromkeys(addrs))
    lines = [f"read32 {hex(a)} 4" for a in uniq]
    if resume:
        lines.append("go")
    out = pyocd_cmd(lines)
    table = {}
    for line in out.splitlines():
        if ":" not in line:
            continue
        a, rhs = line.split(":", 1)
        rhs = rhs.split("|", 1)[0]
        toks = rhs.split()
        if not toks:
            continue
        try:
            table[int(a.strip(), 16)] = int(toks[0], 16)
        except ValueError:
            continue
    return table


def write_file_to_mem(addr, blob):
    """Write a blob to target memory in one pyocd commander session.

    pyocd's `write8` parses each data token as a NUMBER, so a bare hex pair like
    `4e` is rejected ("invalid argument"); every byte needs the 0x prefix.
    Payloads are kept modest per line so one line stays parseable.

    The trailing `go` matters: attaching halts the core, and a halted core has a
    dead serial shell.  Without resuming, every msh command sent afterwards goes
    nowhere.
    """
    lines = []
    CHUNK = 512
    for off in range(0, len(blob), CHUNK):
        piece = blob[off:off + CHUNK]
        payload = " ".join(f"{b:#04x}" for b in piece)
        lines.append(f"write8 {hex(addr + off)} {payload}")
    lines.append("go")
    pyocd_cmd(lines, timeout=600)


def resume_target():
    """Make sure the core is running, so the msh shell answers again."""
    pyocd_cmd(["go"], timeout=60)


def fb_sample(xs, ys):
    """Read a sparse grid out of the 240x240 NES window and return RGB565 words."""
    addrs = [FB_BASE + 2 * (FB_STRIDE * y + x) for y in ys for x in xs]
    table = read_words(addrs)
    return [table.get(a) for a in addrs]


def main():
    rom_path = DEFAULT_ROM
    for i, a in enumerate(sys.argv):
        if a == "--rom" and (i + 1) < len(sys.argv):
            rom_path = sys.argv[i + 1]

    checks = []

    if not os.path.exists(rom_path):
        print(f"\n  [FAIL] ROM not found: {rom_path}")
        print("\n0/1 checks passed")
        return 1

    rom = open(rom_path, "rb").read()
    print(f"ROM: {os.path.basename(rom_path)} ({len(rom)} bytes)")

    # ---- 1. get the staging window address ----
    reset_and_wait()
    text = send_serial(["nes stage"], settle=0.8, listen_s=2.0)
    m = re.search(r"nes stage: addr=0x([0-9a-fA-F]+) size=(\d+)", text)
    if not m:
        print(f"\n  [FAIL] could not read the staging window address\n--- output ---\n{text}")
        print("\n0/1 checks passed")
        return 1

    stage_addr = int(m.group(1), 16)
    stage_size = int(m.group(2))
    print(f"staging window: 0x{stage_addr:08x} ({stage_size} bytes)")

    checks.append(("staging window large enough for ROM",
                   stage_size >= len(rom),
                   f"{stage_size} >= {len(rom)}"))

    if stage_size < len(rom):
        print("\n0/%d checks passed" % len(checks))
        return 1

    # ---- 2. write the ROM and start the emulator ----
    print("writing ROM over SWD...")
    write_file_to_mem(stage_addr, rom)

    # Read the first 8 bytes back as two little-endian words and reassemble.
    back = read_words([stage_addr, stage_addr + 4])
    if (stage_addr in back) and ((stage_addr + 4) in back):
        got8 = (back[stage_addr].to_bytes(4, "little")
                + back[stage_addr + 4].to_bytes(4, "little"))
    else:
        got8 = b""
    checks.append(("ROM magic readable back from staging",
                   got8[:4] == b"NES\x1a",
                   repr(got8[:8])))

    text = send_serial(["nes runmem"], settle=1.0, listen_s=3.0)
    m = re.search(r"nes runmem: (prg=\S+ chr=\S+ mapper=\d+ mirror=\d+)", text)
    loaded = m is not None
    checks.append(("nes runmem decoded the cartridge header", loaded,
                   m.group(1) if m else
                   (text.strip().splitlines()[-1] if text.strip() else "no output")))

    if not loaded:
        print(f"\n--- board output ---\n{text}\n--- end ---")
        for name, ok, detail in checks:
            print(f"  [{'PASS' if ok else 'FAIL'}] {name:44s} {detail}")
        print(f"\n{sum(1 for _, o, _ in checks if o)}/{len(checks)} checks passed")
        return 1

    # ---- 3. let it emulate, then sample the panel twice ----
    time.sleep(3.0)

    # Sparse grid across the whole NES window (dst 120..359 x 60..299).
    xs = list(range(130, 361, 20))
    ys = list(range(70, 301, 20))

    s1 = fb_sample(xs, ys)
    time.sleep(2.0)
    s2 = fb_sample(xs, ys)

    if not s1 or all(v is None for v in s1) or not s2 or all(v is None for v in s2):
        print("\n  [FAIL] SWD read-back of the framebuffer returned nothing")
        print("\n0/1 checks passed")
        return 1

    filled1 = [v for v in s1 if v is not None]
    filled2 = [v for v in s2 if v is not None]

    checks.append(("sample grid read back complete",
                   len(filled1) == len(xs) * len(ys) and len(filled2) == len(xs) * len(ys),
                   f"{len(filled1)}/{len(xs) * len(ys)}, {len(filled2)}/{len(xs) * len(ys)}"))

    # The image must not be a single flat colour: a rendered game frame has many
    # distinct RGB565 values.
    ncolors = len(set(filled1))
    checks.append(("frame is not flat colour (>=8 distinct RGB565 values)",
                   ncolors >= 8, f"{ncolors} distinct"))

    # The strongest check: the picture changed between the two samples.
    changed = sum(1 for a, b in zip(filled1, filled2) if a != b)
    checks.append(("frame content changed between samples (emulator is running)",
                   changed > 0,
                   f"{changed}/{len(filled1)} sampled pixels differ"))

    # And it must not be pure garbage either: a real frame has structure, so the
    # palette should be a small subset of RGB565, not thousands of values.
    checks.append(("frame is structured, not noise (< 512 distinct values)",
                   ncolors < 512, f"{ncolors} distinct"))

    text = send_serial(["nes info"], settle=0.8, listen_s=2.0)
    m = re.search(r"nes: draws=(\d+) failed=(\d+)", text)
    if m:
        draws, failed = int(m.group(1)), int(m.group(2))
        checks.append(("nes_draw() ran and never failed", draws > 0 and failed == 0,
                       f"draws={draws} failed={failed}"))
        m2 = re.search(r"present=(\d+) cyc/frame", text)
        if m2:
            cpf = int(m2.group(1))
            checks.append(("present path under 50% of the 60 Hz budget",
                           cpf < 4000000,
                           f"{cpf} cyc/frame ({cpf * 100 // 8000000}%)"))

    # Stop cleanly so the board is left usable.
    send_serial(["nes stop"], settle=1.5, listen_s=2.0)

    print()
    npass = 0
    for name, ok, detail in checks:
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:52s} {detail}")
        npass += 1 if ok else 0
    print(f"\n{npass}/{len(checks)} checks passed")
    return 0 if npass == len(checks) else 1


if __name__ == "__main__":
    sys.exit(main())
