"""
capture_reset.py — 只复位（不重新烧录）并通过串口抓取启动日志。

用于快速确认启动横幅、microSD 探测、字库来源、UI 状态迁移的**时序**，
而不必每次都跑一遍烧录。默认只打印控制类日志行，自动跳过 U 盘文件正文
dump（那是 verify_ui_com5.py 的职责）。

用法：
  SERIAL_PORT=COM5 python capture_reset.py [capture_seconds]
  SERIAL_PORT=COM5 python capture_reset.py 16 --raw     # 不过滤，打印全部字节
"""
import os
import serial
import subprocess
import sys
import time

OCD_BIN = os.environ.get("OPENOCD_BIN", "D:/software/ST/OpenOCD/bin/openocd.exe")
OCD_SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", "D:/software/ST/OpenOCD/share/openocd/scripts")
PORT = os.environ.get("SERIAL_PORT", "COM5")
BAUD = 115200
CAPTURE_SEC = float(sys.argv[1]) if len(sys.argv) > 1 else 16.0

KEYWORDS = ("Init", "Heap", "USB", "[SD  ]", "[UI  ]", "[FONT]",
            "Mounted", "Waiting", "mount", "fail", "FAILED")


def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.3)

    cmd = [OCD_BIN, "-s", OCD_SCRIPTS,
           "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg",
           "-c", "init", "-c", "reset run", "-c", "shutdown"]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    print("reset exit=%d" % p.returncode)
    if p.returncode != 0:
        print(p.stdout[-1200:])
        print(p.stderr[-1200:])
        ser.close()
        sys.exit(3)

    buf = b""
    t0 = time.time()
    while time.time() - t0 < CAPTURE_SEC:
        d = ser.read(256)
        if d:
            buf += d
    ser.close()

    txt = buf.decode("utf-8", "replace")
    if "--raw" in sys.argv:
        print(txt)
        return
    for line in txt.splitlines():
        if any(k in line for k in KEYWORDS):
            print(line)


if __name__ == "__main__":
    main()
