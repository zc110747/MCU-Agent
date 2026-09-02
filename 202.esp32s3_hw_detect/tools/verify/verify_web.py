"""Verify 1: Web server reachability, status API, auth guard, logs API.

Checks:
  1. GET  /            -> 200, HTML dashboard served
  2. GET  /api/status  -> 200, JSON with device/firmware/uptime/free_heap/ws_port
  3. POST /api/gpio    -> 401 without auth (auth guard works)
  4. GET  /api/wifi    -> 200, JSON (ssid may be empty in AP mode)
  5. GET  /api/logs    -> 200, log text

Usage: python verify_web.py [device_ip]
"""
from rhd_common import Result, DEVICE_IP, http, http_json


def main():
    r = Result("verify_web @ %s" % DEVICE_IP)
    print("Target: http://%s  (Web:80 / WS:81)" % DEVICE_IP)
    print()

    # 1. Dashboard HTML
    code, body = http("GET", "/")
    r.check("GET / returns 200", code == 200, "http %s, %d bytes" % (code, len(body)))
    r.check("dashboard looks like HTML", "<html" in body.lower() and len(body) > 500,
            "len=%d" % len(body))

    # 2. Status API
    code, obj, raw = http_json("GET", "/api/status")
    r.check("GET /api/status returns 200", code == 200)
    if obj is not None:
        r.check("device id starts with esp32s3-",
                str(obj.get("device", "")).startswith("esp32s3-"),
                str(obj.get("device")))
        r.check("firmware field present", "firmware" in obj, str(obj.get("firmware")))
        r.check("uptime is int >= 0", isinstance(obj.get("uptime"), int) and obj["uptime"] >= 0,
                str(obj.get("uptime")))
        r.check("free_heap is int > 0", isinstance(obj.get("free_heap"), int) and obj["free_heap"] > 0,
                str(obj.get("free_heap")))
        r.check("ws_port == 81", obj.get("ws_port") == 81, str(obj.get("ws_port")))
        r.check("wifi mode field present", "wifi" in obj, str(obj.get("wifi")))
    else:
        r.check("status JSON parse", False, raw[:120])

    # 3. Auth guard: POST /api/gpio without credentials must be 401
    code, _ = http("POST", "/api/gpio", body='{"gpio":4,"value":0}', auth=False)
    r.check("POST /api/gpio without auth -> 401", code == 401, "http %s" % code)

    # 4. WiFi config GET (unauthenticated read)
    code, obj, raw = http_json("GET", "/api/wifi", auth=False)
    r.check("GET /api/wifi returns 200", code == 200)
    if obj is not None:
        r.check("wifi ssid field present", "ssid" in obj, repr(obj.get("ssid")))

    # 5. Logs API
    code, body = http("GET", "/api/logs")
    r.check("GET /api/logs returns 200", code == 200, "%d bytes" % len(body))

    sys.exit(r.summary())


if __name__ == "__main__":
    main()
