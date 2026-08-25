# ============================================================================
# tools/gen_upgrade.py
# Build a firmware upgrade package (stm32h7_xx.bin + verify.json) from an app
# image linked at 0x08020000.
#
#   - reads the 4-byte version slot at file offset 0x1000 (0x08021000)
#   - computes HMAC-SHA256 over the whole image with the shared key
#   - writes verify.json { name, len, HMAC-SHA256, version }
#   - copies the image to <name> so it can be dropped on the QSPI U-disk
#
# Usage:
#   python tools/gen_upgrade.py <app.bin> [--name stm32h7_xx.bin] [--out DIR]
# ============================================================================
import argparse
import hashlib
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from boot_secret import BOOT_HMAC_KEY, BOOT_HMAC_KEY_LEN  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin", help="app image linked at 0x08020000")
    ap.add_argument("--name", default=None, help="filename the package has on the U-disk")
    ap.add_argument("--out", default=".", help="output directory")
    args = ap.parse_args()

    if not os.path.isfile(args.bin):
        print("ERR: input '%s' not found" % args.bin)
        return 2

    with open(args.bin, "rb") as f:
        data = f.read()

    if len(data) < (0x1000 + 4):
        print("ERR: image too small to contain a version slot @0x1000")
        return 2

    ver = data[0x1000:0x1000 + 4]
    if any(b > 99 for b in ver):
        print("WARN: version component > 99: %s" % list(ver))

    h = hashlib.sha256()
    hmac = hashlib.sha256()
    # HMAC-SHA256: H((K ^ opad) || H((K ^ ipad) || msg))
    block = BOOT_HMAC_KEY.ljust(64, b"\x00")
    ipad = bytes(b ^ 0x36 for b in block)
    opad = bytes(b ^ 0x5C for b in block)
    inner = hashlib.sha256()
    inner.update(ipad)
    inner.update(data)
    outer = hashlib.sha256()
    outer.update(opad)
    outer.update(inner.digest())
    digest = outer.digest()
    assert len(digest) == 32

    name = args.name or os.path.basename(args.bin)
    out_dir = args.out
    os.makedirs(out_dir, exist_ok=True)

    dst_bin = os.path.join(out_dir, name)
    shutil.copyfile(args.bin, dst_bin)

    verify = {
        "name": name,
        "len": len(data),
        "HMAC-SHA256": digest.hex(),
        "version": "%d.%d.%d.%d" % (ver[0], ver[1], ver[2], ver[3]),
    }
    json_path = os.path.join(out_dir, "verify.json")
    with open(json_path, "w") as f:
        json.dump(verify, f, indent=2)
        f.write("\n")

    print("OK: package written")
    print("    bin : %s (%d bytes)" % (dst_bin, len(data)))
    print("    json: %s" % json_path)
    print("    key : %d bytes" % BOOT_HMAC_KEY_LEN)
    print("    ver : %s" % verify["version"])
    print("    hmac: %s" % verify["HMAC-SHA256"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
