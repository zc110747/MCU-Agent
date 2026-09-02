"""Verify 2: GPIO monitor API — read, drive output, PinManager guard.

Checks:
  1. GET  /api/gpio               -> 4 monitor pins [4,5,6,7]
  2. POST /api/gpio gpio=4 val=1  -> 200 ok, then GET shows state=1
  3. POST /api/gpio gpio=4 val=0  -> 200 ok, then GET shows state=0
  4. POST /api/gpio gpio=13       -> 400 (not a monitor pin, PinManager rejects)
  5. POST /api/gpio gpio=48       -> 400 (RUN LED reserved, must never be writable)

Note: writing GPIO4 switches it to OUTPUT and persists the direction mask
in NVS. After this test GPIO4 stays output-low (harmless default).

Usage: python verify_gpio.py [device_ip]
"""
from rhd_common import Result, DEVICE_IP, http, http_json


def gpio_map():
    code, obj, _ = http_json("GET", "/api/gpio")
    if code != 200 or obj is None or "gpios" not in obj:
        return None
    m = {}
    for g in obj["gpios"]:
        m[g.get("pin")] = (g.get("state"), g.get("dir"))
    return m


def main():
    r = Result("verify_gpio @ %s" % DEVICE_IP)
    print("Target: http://%s/api/gpio" % DEVICE_IP)
    print()

    # 1. Pin map
    m = gpio_map()
    r.check("GET /api/gpio returns pin map", m is not None)
    if m is not None:
        r.check("monitor pins are exactly [4,5,6,7]", sorted(m.keys()) == [4, 5, 6, 7],
                str(sorted(m.keys())))

    # 2. Drive GPIO4 high
    code, body = http("POST", "/api/gpio", body='{"gpio":4,"value":1}')
    r.check("POST gpio4=1 -> 200 ok", code == 200 and "ok" in body, "http %s %s" % (code, body.strip()))
    m = gpio_map()
    if m and 4 in m:
        r.check("GET shows GPIO4 state=1 after write", m[4][0] == 1, "state=%s dir=%s" % m[4])
    else:
        r.check("GET shows GPIO4 state=1 after write", False, "pin map unreadable")

    # 3. Drive GPIO4 low
    code, body = http("POST", "/api/gpio", body='{"gpio":4,"value":0}')
    r.check("POST gpio4=0 -> 200 ok", code == 200 and "ok" in body, "http %s %s" % (code, body.strip()))
    m = gpio_map()
    if m and 4 in m:
        r.check("GET shows GPIO4 state=0 after write", m[4][0] == 0, "state=%s dir=%s" % m[4])
    else:
        r.check("GET shows GPIO4 state=0 after write", False, "pin map unreadable")

    # 4. Non-monitor pin rejected
    code, body = http("POST", "/api/gpio", body='{"gpio":13,"value":1}')
    r.check("POST gpio13 (not a monitor pin) -> 400", code == 400, "http %s" % code)

    # 5. Reserved RUN LED pin rejected
    code, body = http("POST", "/api/gpio", body='{"gpio":48,"value":1}')
    r.check("POST gpio48 (RUN LED, reserved) -> 400", code == 400, "http %s" % code)

    sys.exit(r.summary())


if __name__ == "__main__":
    main()
