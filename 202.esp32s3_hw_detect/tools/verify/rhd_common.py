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
