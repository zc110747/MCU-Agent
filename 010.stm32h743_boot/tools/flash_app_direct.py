# ============================================================================
# tools/flash_app_direct.py
# Flash an app image + matching system config sector straight to the target
# with OpenOCD (bypasses the U-disk upgrade flow). Used to verify the
# bootloader's JUMP path quickly: after this, a reset should make the
# bootloader validate the config and jump to the app.
#
# Layout written:
#   0x08020000  app image (stm32h7_xx.bin)
#   0x081E0000  config sector (app_config_t, 64 bytes, CRC32 protected)
#
# Usage:
#   python tools/flash_app_direct.py <app.bin> [--openocd D:/Software/openocd/bin/openocd.exe]
# ============================================================================
import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from boot_secret import (BOOT_HMAC_KEY, BOOT_HMAC_KEY_LEN,  # noqa: E402
                         APP_CFG_MAGIC, APP_CFG_STATUS_OK)


def hmac_sha256(key, msg):
    block = key.ljust(64, b"\x00")
    ipad = bytes(b ^ 0x36 for b in block)
    opad = bytes(b ^ 0x5C for b in block)
    inner = hashlib.sha256(ipad + msg).digest()
    return hashlib.sha256(opad + inner).digest()


def build_config(data):
    ver = data[0x1000:0x1000 + 4]
    digest = hmac_sha256(BOOT_HMAC_KEY, data)
    blob = bytearray(64)
    struct.pack_into("<I", blob, 0, APP_CFG_MAGIC)        # magic
    struct.pack_into("<I", blob, 4, len(data))            # app_len
    blob[8:12] = ver                                      # version[4]
    blob[12:44] = digest                                  # app_hmac[32]
    struct.pack_into("<I", blob, 44, APP_CFG_STATUS_OK)   # status
    crc = zlib.crc32(bytes(blob[0:48])) & 0xFFFFFFFF      # CRC32 over [0,48) (excl. self)
    struct.pack_into("<I", blob, 48, crc)                 # crc32
    return bytes(blob), ver, digest


def find_openocd(arg):
    if arg:
        return arg
    env = os.environ.get("OPENOCD_BIN")
    if env and os.path.isfile(env):
        return env
    # common install location on this workstation
    cand = "D:/Software/openocd/bin/openocd.exe"
    return cand if os.path.isfile(cand) else "openocd"


def to_fwd(p):
    """OpenOCD parses its -c arguments as TCL, where backslashes are escapes.
    Use forward slashes so Windows paths survive intact."""
    return os.path.abspath(p).replace("\\", "/")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin", help="app image linked at 0x08020000")
    ap.add_argument("--openocd", default=None)
    args = ap.parse_args()

    if not os.path.isfile(args.bin):
        print("ERR: %s not found" % args.bin)
        return 2

    with open(args.bin, "rb") as f:
        data = f.read()
    if len(data) < (0x1000 + 4):
        print("ERR: image too small for version slot")
        return 2

    cfg_blob, ver, digest = build_config(data)
    print("config: ver=%s.%s.%s.%s len=%d hmac=%s" % (
        ver[0], ver[1], ver[2], ver[3], len(data), digest.hex()))

    tmp = tempfile.mkdtemp(prefix="bl_flash_")
    cfg_path = os.path.join(tmp, "config.bin")
    with open(cfg_path, "wb") as f:
        f.write(cfg_blob)

    oc = find_openocd(args.openocd)
    if not os.path.isfile(oc):
        print("ERR: openocd not found at %s" % oc)
        print("     install OpenOCD or pass --openocd PATH")
        return 2

    cmd = [
        oc, "-f", "interface/stlink.cfg",
        "-c", "transport select swd",
        "-f", "target/stm32h7x.cfg",
        # openocd's stm32h7x.cfg only registers bank1 on this part; the config
        # sector lives in bank2 (0x081E0000), so declare it explicitly BEFORE init.
        "-c", "flash bank stm32h7x.bank2 stm32h7x 0x08100000 0 0 0 stm32h7x.cpu0",
        "-c", "init",
        "-c", "reset_config srst_only",
        "-c", "program %s verify 0x08020000" % to_fwd(args.bin),
        "-c", "program %s verify 0x081E0000" % to_fwd(cfg_path),
        "-c", "reset run",
        "-c", "exit",
    ]
    print("running: %s" % " ".join(cmd))
    rc = subprocess.run(cmd)
    return 0 if rc.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
