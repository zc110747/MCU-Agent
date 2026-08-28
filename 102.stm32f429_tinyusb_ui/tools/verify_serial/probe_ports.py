"""probe_ports.py - 确定 STM32F429 USART3 调试控制台落在哪个 COM 口。

同时打开候选口 (COM4, COM6, 并尝试 COM5)，用 OpenOCD 触发一次板子 reset run，
抓取 25s，打印每个口收到的字节数与前 1500 字符，从而定位真正承载 printf 的串口。

用法: python probe_ports.py
"""
import serial
import subprocess
import time

OCD = "D:/software/ST/OpenOCD/bin/openocd.exe"
SCR = "D:/software/ST/OpenOCD/share/openocd/scripts"
CANDIDATES = ["COM4", "COM6", "COM5"]
BAUD = 115200
CAPTURE_SEC = 25


def main():
    opened = {}
    for p in CANDIDATES:
        try:
            s = serial.Serial(p, BAUD, timeout=0.3)
            opened[p] = s
            print("[open] %s" % p)
        except Exception as e:
            print("[skip] %s -> %s" % (p, e))

    if not opened:
        print("NO PORT OPENABLE")
        return

    print("[reset] rebooting board via OpenOCD ...")
    cmd = [OCD, "-s", SCR,
           "-f", "interface/stlink.cfg",
           "-f", "target/stm32f4x.cfg",
           "-c", "init",
           "-c", "reset run",
           "-c", "shutdown"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    print("    openocd exit=%d" % r.returncode)

    bufs = {p: b"" for p in opened}
    t0 = time.time()
    while time.time() - t0 < CAPTURE_SEC:
        for p, s in opened.items():
            d = s.read(256)
            if d:
                bufs[p] += d
    for p, s in opened.items():
        s.close()

    for p in CANDIDATES:
        if p not in opened:
            print("\n==== %s : (not opened) ====" % p)
            continue
        txt = bufs[p].decode("utf-8", "replace")
        print("\n==== %s : bytes=%d ====" % (p, len(bufs[p])))
        print(txt[:1500] if txt.strip() else "(no bytes received)")


if __name__ == "__main__":
    main()
