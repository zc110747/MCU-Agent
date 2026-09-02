"""Verify 3: External ADC (ADS1115) API — sample read + config endpoints.

Checks:
  1. POST /api/adc/read (auth) -> 200, JSON with ch0..ch3, each has raw+voltage
  2. voltage values are numeric
  3. GET /api/adc/config       -> 200, fsr (full-scale range) numeric

Note: if ADS1115 is not wired, raw values read 0 and voltage 0.0. The
endpoint/protocol still passes; an explicit WARN is printed so the operator
knows a real sensor must be connected for full signal-chain verification.

Usage: python verify_adc.py [device_ip]
"""
import sys

from rhd_common import Result, DEVICE_IP, http, http_json


def main():
    r = Result("verify_adc @ %s" % DEVICE_IP)
    print("Target: http://%s/api/adc/*" % DEVICE_IP)
    print()

    # 1. Read 4 channels
    code, obj, raw = http_json("POST", "/api/adc/read", body="")
    r.check("POST /api/adc/read returns 200", code == 200, "http %s" % code)
    if obj is None:
        r.check("adc read JSON parse", False, raw[:120])
        sys.exit(r.summary())

    chs_ok = all(("ch%d" % i) in obj for i in range(4))
    r.check("response contains ch0..ch3", chs_ok, str(sorted(obj.keys())))

    numeric_ok = True
    all_zero = True
    for i in range(4):
        ch = obj.get("ch%d" % i, {})
        rawv, volt = ch.get("raw"), ch.get("voltage")
        if not isinstance(rawv, int) or not isinstance(volt, (int, float)):
            numeric_ok = False
        if rawv:
            all_zero = False
    r.check("all channels have numeric raw+voltage", numeric_ok)

    # 2. WARN (not FAIL) if ADS1115 seems absent
    if all_zero:
        print("[WARN] all raw readings are 0 -> ADS1115 may not be wired.")
        print("       Protocol verified; connect ADS1115 (SDA=8 SCL=9 addr=0x48)")
        print("       and a real signal for full signal-chain verification.")

    # 3. Config endpoint
    code, obj, raw = http_json("GET", "/api/adc/config")
    r.check("GET /api/adc/config returns 200", code == 200)
    if obj is not None:
        r.check("fsr is numeric", isinstance(obj.get("fsr"), (int, float)), str(obj.get("fsr")))
    else:
        r.check("adc config JSON parse", False, raw[:120])

    sys.exit(r.summary())


if __name__ == "__main__":
    main()
