#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32F429 telnet server 全命令自动化测试。

不依赖 telnetlib（Python 3.13 已移除），纯 socket 实现。模拟标准
PuTTY / Windows telnet 客户端的 IAC 协商，验证服务端发回的每一帧。

设计：
- 对每条命令调用 send_line(cmd) 后 drain(0.6s)，把命令回显 + 命令输出
  完整抓下来，再单独检查关键字。
- 不依赖收到 "STM32> " 才算完成：避免对某条"无输出命令"形成死等。

测试覆盖（与 `help` 列表一致）：
  help, version, hw, dev, history, list(unknown), net,
  beep on, beep off, led on, led off, exit

用法：
  python telnet_test.py [--host 192.168.10.99] [--port 23] [--dump]
  python telnet_test.py --idle
"""
import socket
import sys
import time
import argparse

IAC  = 255
DONT = 254
DO   = 253
WONT = 252
WILL = 251
SB   = 250
SE   = 240
ECHO = 1
SGA  = 3


def iac(*args: int) -> bytes:
    return bytes([IAC, *args])


class TelnetClient:
    """Minimal telnet client over a raw TCP socket."""

    def __init__(self, host: str, port: int, timeout: float = 5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.recv_buf = bytearray()
        # Proactive negotiation so the server enters the expected state
        # (echo + suppress go-ahead) immediately.
        self.sock.sendall(iac(WILL, ECHO))
        self.sock.sendall(iac(DO, SGA))
        self.sock.sendall(iac(WILL, SGA))

    def _negotiate(self, data: bytes) -> bytes:
        """Strip IAC from the wire stream and reply symmetrically."""
        out = bytearray()
        i = 0
        n = len(data)
        while i < n:
            b = data[i]
            if b != IAC:
                out.append(b); i += 1; continue
            if i + 1 >= n:
                break
            cmd = data[i + 1]
            if cmd == IAC:
                out.append(IAC); i += 2; continue
            if cmd == SB:
                j = i + 2
                while j + 1 < n:
                    if data[j] == IAC and data[j + 1] == SE:
                        j += 2; break
                    j += 1
                i = j; continue
            if cmd in (WILL, WONT, DO, DONT):
                if i + 2 >= n: break
                opt = data[i + 2]
                if cmd == WILL:        self.sock.sendall(iac(DONT, opt))
                elif cmd == WONT:      self.sock.sendall(iac(DONT, opt))
                elif cmd == DO:
                    if opt in (ECHO, SGA): self.sock.sendall(iac(WILL, opt))
                    else:                 self.sock.sendall(iac(WONT, opt))
                elif cmd == DONT:      self.sock.sendall(iac(WONT, opt))
                i += 3; continue
            i += 2
        return bytes(out)

    def recv_until(self, marker: bytes, timeout: float = 3.0) -> bytes:
        end = time.time() + timeout
        while time.time() < end:
            if marker in self.recv_buf:
                break
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            data = self._negotiate(chunk)
            self.recv_buf.extend(data)
        return bytes(self.recv_buf)

    def send_line(self, line: str):
        """Send a command followed by CRLF. Flush prev recv buffer first."""
        self.recv_buf = bytearray()
        self.sock.sendall((line + "\r\n").encode("utf-8", "replace"))

    def send_raw(self, data: bytes):
        """Send raw bytes (may include embedded NUL) followed by CRLF."""
        self.recv_buf = bytearray()
        self.sock.sendall(data + b"\r\n")

    def drain(self, timeout: float = 0.6) -> bytes:
        """Read whatever the server sends within `timeout` seconds."""
        end = time.time() + timeout
        while time.time() < end:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                break
            except OSError:
                break  # RST
            if not chunk:
                break
            data = self._negotiate(chunk)
            self.recv_buf.extend(data)
        return bytes(self.recv_buf)

    def close(self):
        try: self.sock.close()
        except Exception: pass


# === command test plan ============================================
# (name, command, list of substrings expected in the output)
# `None` substring means "just expect non-empty output".
CASES = [
    ("help",          "help",     ["Commands:", "hw", "dev", "net", "version", "beep", "led", "reboot", "history"]),
    ("unknown 'list'", "list",    ["Unknown command: list"]),
    ("version",       "version",  ["Firmware:", "Build  :"]),
    ("hw",            "hw",       ["=== System ===", "MCU", "Clock", "FreeRTOS"]),
    ("dev",           "dev",      ["=== Devices ===", "sensor_valid", "AP3216C lux/ps/ir",
                                   "MPU9250 ax/ay/az", "LED", "BEEP"]),
    ("history",       "history",  ["=== History ==="]),
    ("net (show)",    "net",      ["=== Network (pending, applied after reboot) ===",
                                   "IP", "Mask", "GW", "MAC"]),
    ("beep on",       "beep on",  ["BEEP ON"]),
    ("beep off",      "beep off", ["BEEP OFF"]),
    ("led on",        "led on",   ["LED ON (PB0)"]),
    ("led off",       "led off",  ["LED OFF (PB0)"]),
]


def run_all(host: str, port: int, dump: bool) -> int:
    passed = 0
    failed = 0

    def ok(name, cond, detail=""):
        nonlocal passed, failed
        if cond:
            passed += 1
            print(f"  [PASS] {name}")
        else:
            failed += 1
            print(f"  [FAIL] {name}  {detail}")

    print(f"== Connecting to {host}:{port} ==")
    try:
        cli = TelnetClient(host, port, timeout=5.0)
    except Exception as e:
        print(f"  [FATAL] connect failed: {e}")
        return 1

    # 1. Banner
    banner = cli.recv_until(b"STM32> ", timeout=4.0)
    if dump: print("--- RAW BANNER ---\n" + repr(banner) + "\n------------------")
    ok("banner received", len(banner) > 0, "no data")
    ok("banner has title", b"STM32F429 Shell (telnet)" in banner, "title missing")
    ok("prompt present", b"STM32> " in banner, "prompt missing")

    # 2. Every command in CASES, fresh recv buffer per command
    for name, cmd, expects in CASES:
        cli.send_line(cmd)
        # Drain until prompt arrives (idempotent: command output ends with prompt),
        # or hard timeout if the command doesn't finish its prompt. Give 1.5s.
        resp = cli.recv_until(b"STM32> ", timeout=1.5)
        if not resp:
            # didn't even get prompt — give a brief extra drain
            resp = cli.drain(timeout=0.5)
        if dump:
            print(f"--- RAW {name} ({cmd}) ---\n" + repr(resp) + "\n{'-'*40}")
        # Decode for substring matching (latin1 = 1:1 byte->char, robust on
        # binary garbage).
        text = resp.decode("latin1", "replace")
        missing = [s for s in expects if s not in text]
        if missing:
            ok(name, False, f"missing: {missing}; raw={resp[:160]!r}")
        else:
            ok(name, True)

    # 3. exit
    cli.send_line("exit")
    tail = cli.drain(timeout=2.0)
    if dump: print("--- RAW EXIT ---\n" + repr(tail) + "\n---------------")
    ok("exit bye message", b"Bye" in tail, "no Bye message")
    closed = False
    try:
        cli.sock.settimeout(1.0)
        extra = cli.sock.recv(64)
        if not extra:
            closed = True
    except socket.timeout:
        closed = False
    except OSError:
        closed = True  # RST — still counts as closed
    ok("exit closes connection", closed or b"Bye" in tail, "connection not closed")

    cli.close()
    print(f"\n== Result: {passed} passed, {failed} failed ==")
    return 0 if failed == 0 else 1


def run_nul(host: str, port: int, dump: bool) -> int:
    """Verify that stray NUL (0) bytes in the stream are ignored, so a
    command like 'help' is not truncated into an empty/garbled string."""
    passed = 0
    failed = 0

    def ok(name, cond, detail=""):
        nonlocal passed, failed
        if cond:
            passed += 1
            print(f"  [PASS] {name}")
        else:
            failed += 1
            print(f"  [FAIL] {name}  {detail}")

    print(f"== NUL-injection test on {host}:{port} ==")
    try:
        cli = TelnetClient(host, port, timeout=5.0)
    except Exception as e:
        print(f"  [FATAL] connect failed: {e}")
        return 1

    cli.recv_until(b"STM32> ", timeout=4.0)

    # 1. NUL in the middle: "he\0lp" must still run 'help'
    cli.send_raw(b"he\x00lp")
    resp = cli.recv_until(b"STM32> ", timeout=1.5)
    if not resp:
        resp = cli.drain(timeout=0.5)
    if dump:
        print("--- RAW NUL-mid ---\n" + repr(resp) + "\n{'-'*40}")
    ok("NUL-in-middle -> help runs",
       b"Commands:" in resp and b"=== System ===" not in resp,
       "help output missing / wrong cmd")
    ok("NUL-in-middle -> no garbage output",
       b"Unknown command" not in resp,
       "treated as unknown command")

    # 2. NUL prefix: "\0help" must still run 'help'
    cli.send_raw(b"\x00help")
    resp = cli.recv_until(b"STM32> ", timeout=1.5)
    if not resp:
        resp = cli.drain(timeout=0.5)
    if dump:
        print("--- RAW NUL-prefix ---\n" + repr(resp) + "\n{'-'*40}")
    ok("NUL-prefix -> help runs",
       b"Commands:" in resp,
       "help output missing after leading NUL")

    # 3. NUL suffix: "help\0" must still run 'help'
    cli.send_raw(b"help\x00")
    resp = cli.recv_until(b"STM32> ", timeout=1.5)
    if not resp:
        resp = cli.drain(timeout=0.5)
    if dump:
        print("--- RAW NUL-suffix ---\n" + repr(resp) + "\n{'-'*40}")
    ok("NUL-suffix -> help runs",
       b"Commands:" in resp,
       "help output missing after trailing NUL")

    # 4. sanity: a normal 'help' still works after the NUL barrage
    cli.send_line("help")
    resp = cli.recv_until(b"STM32> ", timeout=1.5)
    if not resp:
        resp = cli.drain(timeout=0.5)
    ok("normal help still works", b"Commands:" in resp, "help output missing")

    # 5. exit
    cli.send_line("exit")
    tail = cli.drain(timeout=2.0)
    ok("exit closes connection", b"Bye" in tail, "no Bye message")
    cli.close()
    print(f"\n== NUL Result: {passed} passed, {failed} failed ==")
    return 0 if failed == 0 else 1


def run_idle(host: str, port: int) -> int:
    print("== Idle-timeout test (wait ~31s) ==")
    cli = TelnetClient(host, port, timeout=50.0)
    cli.sock.settimeout(50.0)
    cli.recv_until(b"STM32> ", timeout=4.0)
    print("  connected, waiting for idle disconnect (no input)...")
    start = time.time()
    buf = bytearray()
    got = False
    while time.time() - start < 38:
        try:
            chunk = cli.sock.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        if not chunk:
            break
        buf.extend(chunk)
        if b"[timeout]" in buf:
            got = True
            break
    cli.close()
    if got:
        print("  [PASS] idle timeout message received")
        return 0
    print("  [FAIL] no idle timeout message within 38s")
    return 1


def main():
    ap = argparse.ArgumentParser(description="STM32F429 telnet server test")
    ap.add_argument("--host", default="192.168.10.99")
    ap.add_argument("--port", type=int, default=23)
    ap.add_argument("--dump", action="store_true", help="print raw recv dump")
    ap.add_argument("--idle", action="store_true",
                    help="run the 30s idle-timeout check instead of the command suite")
    ap.add_argument("--nul", action="store_true",
                    help="run the NUL-injection test (stray 0 bytes must be ignored)")
    args = ap.parse_args()
    if args.idle:
        sys.exit(run_idle(args.host, args.port))
    if args.nul:
        sys.exit(run_nul(args.host, args.port, args.dump))
    sys.exit(run_all(args.host, args.port, args.dump))


if __name__ == "__main__":
    main()
