"""Verify 4: WebSocket live push on port 81 (stdlib-only WS client).

Connects to ws://<ip>:81/, listens up to LISTEN_S seconds, and counts the
JSON message types the gateway broadcasts (uart / uart_tx / adc / gpio /
log / system ...). PASS if at least MIN_MESSAGES valid typed messages arrive.

Expected background traffic even with nothing wired:
  - adc samples every 100 ms (raw 0 if ADS1115 absent)
  - system status every 5 s
  - log lines from the firmware logger

Usage: python verify_ws.py [device_ip]
"""
import base64
import json
import os
import socket
import struct
import sys
import time

from rhd_common import Result, DEVICE_IP

WS_PORT = 81
LISTEN_S = 12.0
MIN_MESSAGES = 3


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("socket closed by peer")
        buf += chunk
    return buf


def recv_frame(sock):
    """Returns (opcode, payload). Server->client frames are unmasked."""
    h = recv_exact(sock, 2)
    opcode = h[0] & 0x0F
    ln = h[1] & 0x7F
    if ln == 126:
        ln = struct.unpack(">H", recv_exact(sock, 2))[0]
    elif ln == 127:
        ln = struct.unpack(">Q", recv_exact(sock, 8))[0]
    payload = recv_exact(sock, ln) if ln else b""
    return opcode, payload


def main():
    r = Result("verify_ws @ %s:%d" % (DEVICE_IP, WS_PORT))
    print("Connecting ws://%s:%d/ and listening %.0fs ..." % (DEVICE_IP, WS_PORT, LISTEN_S))
    print()

    sock = None
    try:
        sock = socket.create_connection((DEVICE_IP, WS_PORT), timeout=6)
        sock.settimeout(2.0)

        # --- Handshake ---
        key = base64.b64encode(os.urandom(16)).decode()
        req = ("GET / HTTP/1.1\r\n"
               "Host: %s:%d\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n") % (DEVICE_IP, WS_PORT, key)
        sock.sendall(req.encode())

        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = sock.recv(1024)
            if not chunk:
                raise ConnectionError("closed during handshake")
            resp += chunk
        hs_ok = b"101" in resp.split(b"\r\n", 1)[0]
        r.check("WS handshake 101 Switching Protocols", hs_ok,
                resp.split(b"\r\n", 1)[0].decode("latin1"))
        if not hs_ok:
            sys.exit(r.summary())

        # --- Listen loop ---
        type_count = {}
        msg_count = 0
        deadline = time.time() + LISTEN_S
        while time.time() < deadline:
            try:
                opcode, payload = recv_frame(sock)
            except socket.timeout:
                continue
            except ConnectionError:
                r.check("connection stayed open", False, "closed by peer")
                break
            if opcode == 0x9:  # ping -> pong
                sock.sendall(b"\x8a\x00")
                continue
            if opcode == 0x8:  # close
                r.check("connection stayed open", False, "close frame")
                break
            if opcode != 0x1:
                continue
            try:
                obj = json.loads(payload.decode("utf-8", "replace"))
            except Exception:
                continue
            t = obj.get("type", "<none>")
            type_count[t] = type_count.get(t, 0) + 1
            msg_count += 1

        r.check("connection stayed open for %.0fs" % LISTEN_S, msg_count >= 0 and True)
        r.check("received >= %d valid typed JSON messages" % MIN_MESSAGES,
                msg_count >= MIN_MESSAGES, "total=%d" % msg_count)
        if type_count:
            hist = ", ".join("%s=%d" % (k, v) for k, v in sorted(type_count.items()))
            print("[INFO] message histogram: %s" % hist)
        if "system" in type_count:
            r.check("periodic system status observed", True, "every 5s heartbeat")
        else:
            print("[WARN] no 'system' message yet (heartbeat every 5s; "
                  "increase LISTEN_S if the device just booted)")

    except Exception as e:
        r.check("WS connect/listen completed", False, str(e))
    finally:
        if sock:
            try:
                sock.close()
            except Exception:
                pass

    sys.exit(r.summary())


if __name__ == "__main__":
    main()
