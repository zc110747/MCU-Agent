"""Run all 202.esp32s3_hw_detect verification scripts and aggregate results.

Usage:
    python run_all.py                 # device at 192.168.4.1 (AP mode)
    python run_all.py 192.168.1.50    # device at a given IP (STA mode)

Exit code: 0 = all scripts passed, 1 = at least one failure.
"""
import subprocess
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))

SCRIPTS = [
    "verify_web.py",
    "verify_gpio.py",
    "verify_adc.py",
    "verify_ws.py",
    "verify_interface.py",
]


def main():
    ip = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("RHD_IP", "192.168.4.1")
    print("#" * 60)
    print("# ESP32-S3 Remote Hardware Debugger - end-to-end verification")
    print("# Target: %s   Scripts: %d" % (ip, len(SCRIPTS)))
    print("#" * 60)

    results = {}
    for script in SCRIPTS:
        path = os.path.join(HERE, script)
        print()
        print(">>> %s" % script)
        print("-" * 60)
        proc = subprocess.run([sys.executable, path, ip], cwd=HERE)
        results[script] = proc.returncode

    print()
    print("#" * 60)
    print("# SUMMARY")
    print("#" * 60)
    ok = 0
    for script, rc in results.items():
        status = "PASS" if rc == 0 else "FAIL"
        print("  [%s] %s" % (status, script))
        ok += (rc == 0)
    total = len(SCRIPTS)
    print("-" * 60)
    print("TOTAL: %d/%d scripts passed" % (ok, total))
    sys.exit(0 if ok == total else 1)


if __name__ == "__main__":
    main()
