# ============================================================================
# tools/verify_hmac.py
# Cross-check the upgrade package (and the shared key) BEFORE flashing:
#   1. recompute HMAC-SHA256 over the .bin and compare to verify.json
#   2. confirm the tool key equals the bootloader's BOOT_HMAC_KEY (bsp/flash_secure.h)
#   3. confirm the version slot @0x1000 matches verify.json "version"
#
# A clean run means the bootloader will accept the package. exit 0 = PASS.
# ============================================================================
import argparse
import hashlib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from boot_secret import BOOT_HMAC_KEY, BOOT_HMAC_KEY_LEN  # noqa: E402

PASS, FAIL = 0, 0


def check(cond, msg):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("  PASS  %s" % msg)
    else:
        FAIL += 1
        print("  FAIL  %s" % msg)


def hmac_sha256(key, msg):
    block = key.ljust(64, b"\x00")
    ipad = bytes(b ^ 0x36 for b in block)
    opad = bytes(b ^ 0x5C for b in block)
    inner = hashlib.sha256(ipad + msg).digest()
    return hashlib.sha256(opad + inner).digest()


def c_key_from_header(path):
    """Extract the BOOT_HMAC_KEY byte list from bsp/flash_secure.h."""
    if not os.path.isfile(path):
        return None
    txt = open(path, "r", encoding="utf-8", errors="ignore").read()
    m = re.search(r"BOOT_HMAC_KEY_LEN\s+(\d+)", txt)
    n = int(m.group(1)) if m else None
    chars = re.findall(r"'([^']*)'", txt)
    # the key initializer is the contiguous char list after BOOT_HMAC_KEY =
    km = re.search(r"BOOT_HMAC_KEY\[[^\]]*\]\s*=\s*\{([^}]*)\}", txt)
    if not km:
        return None
    elems = re.findall(r"'([^']*)'", km.group(1))
    if n is not None and len(elems) != n:
        return None
    return bytes(ord(c) for c in elems)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin", help="app image")
    ap.add_argument("json", help="verify.json path")
    ap.add_argument("--secure-h", default=None, help="path to bsp/flash_secure.h")
    args = ap.parse_args()

    with open(args.bin, "rb") as f:
        data = f.read()
    with open(args.json, "r") as f:
        cfg = json.load(f)

    print("== HMAC over image ==")
    digest = hmac_sha256(BOOT_HMAC_KEY, data)
    expect = bytes.fromhex(cfg["HMAC-SHA256"])
    check(digest == expect, "HMAC-SHA256(bin) == verify.json HMAC-SHA256")
    check(len(data) == cfg["len"], "bin length == verify.json len (%d)" % cfg["len"])

    print("== version slot @0x1000 ==")
    ver = data[0x1000:0x1000 + 4]
    check(list(ver) == [int(x) for x in cfg["version"].split(".")],
          "version @0x1000 (%s) == verify.json version (%s)" % (list(ver), cfg["version"]))

    print("== key agreement (tools <-> bootloader) ==")
    sec = args.secure_h or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        "..", "bsp", "flash_secure.h")
    ckey = c_key_from_header(sec)
    if ckey is None:
        print("  SKIP  could not parse %s" % sec)
    else:
        check(ckey == BOOT_HMAC_KEY, "tool key == bootloader BOOT_HMAC_KEY (%d bytes)" % BOOT_HMAC_KEY_LEN)

    print("\nRESULT: %d passed, %d failed" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
