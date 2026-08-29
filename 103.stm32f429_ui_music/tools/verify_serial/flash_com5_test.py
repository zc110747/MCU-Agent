"""
flash_com5_test.py — 烧录最新固件并通过 COM5 (USART3 PB10/PB11, 115200 8N1)
抓取启动横幅，验证 "USB 初始化时序修正" 后系统不再跑飞。

流程：
  1. 先打开 COM5（从 t=0 开始抓，避免错过启动横幅）
  2. 用 OpenOCD + STLink 烧录并 reset run
  3. 抓取 ~12 秒串口输出
  4. 识别关键特征串：System Init / SDRAM Init OK / USB Host Init /
     Waiting for USB disk / USB Disk Connected / USB Disk Ready / test.txt

工具路径走环境变量（不写死机器路径）：
  OPENOCD_BIN, OPENOCD_SCRIPTS
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
CAPTURE_SEC = 12

ELF = "build/stm32f429_tinyusb_ui.elf"

MARKERS = [
    "System Init",
    "SDRAM Init OK",
    "FreeRTOS Heap configured",
    "USB Host Init",
    "Waiting for USB disk",
    "USB Disk Connected",
    "USB Disk Mounted",
    "Create test.txt",
    "Write test.txt",
    "Read test.txt",
    "USB Disk Ready",
    "Heap object @",
]


def main():
    # 1) 先开串口，确保启动横幅不丢失
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.3)
    except Exception as e:
        print("OPEN %s FAIL -> %s" % (PORT, e))
        sys.exit(2)
    print("[1] COM5 opened (%s)" % ser.name)

    # 2) 烧录并复位运行
    print("[2] flashing %s via OpenOCD+STLink ..." % ELF)
    cmd = [
        OCD_BIN, "-s", OCD_SCRIPTS,
        "-f", "interface/stlink.cfg",
        "-f", "target/stm32f4x.cfg",
        "-c", "init",
        "-c", "halt",
        "-c", "flash write_image erase %s" % ELF,
        "-c", "verify_image %s" % ELF,
        "-c", "reset run",
        "-c", "shutdown",
    ]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    print("    openocd exit=%d" % p.returncode)
    if p.returncode != 0:
        print(p.stdout[-1500:])
        print(p.stderr[-1500:])
        ser.close()
        sys.exit(3)
    print("    flash OK, device running")

    # 3) 抓取串口输出
    buf = b""
    t0 = time.time()
    while time.time() - t0 < CAPTURE_SEC:
        d = ser.read(256)
        if d:
            buf += d
    ser.close()

    txt = buf.decode("utf-8", "replace")
    print("\n========== COM5 (USART3) capture ==========")
    print(txt if txt.strip() else "(no bytes received)")
    print("========== byte count: %d ==========" % len(buf))

    found = [m for m in MARKERS if m in txt]
    print("\n-- feature markers found: %s" % (found if found else "NONE"))
    if "System Init" in txt and "Waiting for USB disk" in txt:
        print("\nVERDICT: PASS — 启动横幅完整，USB 初始化时序修正生效，系统未跑飞")
    elif "tusb_init FAILED" in txt or "HardFault" in txt or "Error" in txt:
        print("\nVERDICT: FAIL — 固件报告错误")
    else:
        print("\nVERDICT: INCONCLUSIVE — 串口未收到预期横幅（检查 COM5 接线 / USART3 映射）")


if __name__ == "__main__":
    main()
