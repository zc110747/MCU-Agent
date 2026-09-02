"""Shared helpers for 202.esp32s3_hw_detect verification scripts.

Stdlib-only (no external deps). English output only (GBK console safe).

Usage:
    python verify_web.py            # uses default IP 192.168.4.1 (AP mode)
    python verify_web.py 192.168.1.50
    RHD_IP=192.168.1.50 python verify_web.py

Auth: Web config/OTA endpoints use Basic auth, default admin/admin.
"""
import base64
import json
import os
import socket
import sys
import urllib.error
import urllib.request

# Basic auth credentials (default web password from config/app_config.h)
AUTH_USER = "admin"
AUTH_PASS = "admin"
AUTH = base64.b64encode(("%s:%s" % (AUTH_USER, AUTH_PASS)).encode()).decode()

# Device IP: CLI arg > env RHD_IP > default (SoftAP address)
if len(sys.argv) > 1:
    DEVICE_IP = sys.argv[1]
else:
    DEVICE_IP = os.environ.get("RHD_IP", "192.168.4.1")


class Result(object):
    """Pass/fail counter for one verification script."""

    def __init__(self, name):
        self.name = name
        self.passed = 0
        self.failed = 0

    def check(self, desc, ok, detail=""):
        tag = "PASS" if ok else "FAIL"
        line = "[%s] %s" % (tag, desc)
        if detail:
            line += "  (%s)" % detail
        print(line)
        if ok:
            self.passed += 1
        else:
            self.failed += 1
        return ok

    def summary(self):
        total = self.passed + self.failed
        status = "ALL PASS" if self.failed == 0 else "HAS FAILURES"
        print("=" * 50)
        print("%s: %d/%d passed, %d failed -> %s"
              % (self.name, self.passed, total, self.failed, status))
        return 0 if self.failed == 0 else 1


def http(method, path, body=None, auth=True, timeout=6, base=None):
    """HTTP request against the device. Returns (status_code, text_body)."""
    url = "http://%s%s" % (base or DEVICE_IP, path)
    data = body.encode("utf-8") if isinstance(body, str) else body
    req = urllib.request.Request(url, data=data, method=method)
    if auth:
        req.add_header("Authorization", "Basic " + AUTH)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.getcode(), resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:  # connection refused / timeout / DNS
        return -1, str(e)


def http_json(method, path, body=None, auth=True, timeout=6):
    """http() + JSON decode. Returns (status_code, dict_or_None, raw_text)."""
    code, text = http(method, path, body=body, auth=auth, timeout=timeout)
    obj = None
    try:
        obj = json.loads(text)
    except Exception:
        pass
    return code, obj, text


# --------------------------------------------------------------------------
# Minimal stdlib WebSocket client (RFC 6455, text frames only).
# Used to drive `cmd` messages and to observe the device's live `state`
# snapshots on port 81.
# --------------------------------------------------------------------------
WS_PORT = 81


class WSError(Exception):
    pass


class WSClient(object):
    """Blocking WebSocket client. Server->client frames are unmasked."""

    def __init__(self, ip=None, port=WS_PORT, timeout=6):
        self.ip = ip or DEVICE_IP
        self.port = port
        self.timeout = timeout
        self.sock = None

    def connect(self):
        import base64 as _b64
        import os as _os
        import struct as _struct

        self.sock = socket.create_connection((self.ip, self.port), timeout=self.timeout)
        key = _b64.b64encode(_os.urandom(16)).decode()
        req = ("GET / HTTP/1.1\r\n"
               "Host: %s:%d\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n") % (self.ip, self.port, key)
        self.sock.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = self.sock.recv(1024)
            if not chunk:
                raise WSError("closed during handshake")
            resp += chunk
        if b"101" not in resp.split(b"\r\n", 1)[0]:
            raise WSError("handshake failed: %s" % resp.split(b"\r\n", 1)[0].decode("latin1"))
        self.sock.settimeout(0.5)
        return self

    def send(self, obj):
        """Send a JSON object as one masked text frame."""
        import os as _os
        import struct as _struct

        payload = json.dumps(obj).encode("utf-8")
        header = bytearray([0x81])
        n = len(payload)
        mask = _os.urandom(4)
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126)
            header += _struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += _struct.pack(">Q", n)
        header += mask
        masked = bytearray(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(bytes(header) + bytes(masked))

    def _recv_frame(self):
        import struct as _struct

        def exact(n):
            buf = b""
            while len(buf) < n:
                chunk = self.sock.recv(n - len(buf))
                if not chunk:
                    raise WSError("socket closed by peer")
                buf += chunk
            return buf

        h = exact(2)
        opcode = h[0] & 0x0F
        ln = h[1] & 0x7F
        if ln == 126:
            ln = _struct.unpack(">H", exact(2))[0]
        elif ln == 127:
            ln = _struct.unpack(">Q", exact(8))[0]
        payload = exact(ln) if ln else b""
        return opcode, payload

    def recv(self, timeout=None):
        """Return one decoded JSON object, or None on timeout. Raises WSError."""
        import time as _time

        self.sock.settimeout(timeout if timeout is not None else 0.5)
        deadline = None
        if timeout is not None:
            deadline = _time.time() + timeout
        while True:
            try:
                opcode, payload = self._recv_frame()
            except socket.timeout:
                return None
            if opcode == 0x9:                      # ping -> pong
                self.sock.sendall(b"\x8a\x00")
                continue
            if opcode == 0x8:                      # close
                raise WSError("close frame from server")
            if opcode != 0x1:
                continue
            try:
                return json.loads(payload.decode("utf-8", "replace"))
            except Exception:
                continue

    def wait_for(self, predicate, timeout=5.0):
        """Block until predicate(msg) is true. Returns the message or None."""
        import time as _time

        deadline = _time.time() + timeout
        while _time.time() < deadline:
            msg = self.recv(timeout=max(0.05, deadline - _time.time()))
            if msg is None:
                continue
            try:
                if predicate(msg):
                    return msg
            except Exception:
                continue
        return None

    def wait_type(self, mtype, timeout=5.0):
        return self.wait_for(lambda m: m.get("type") == mtype, timeout)

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None
