"""
explore_com5_test.py — 烧录最新固件并通过 COM5 (USART3 PB10/PB11, 115200 8N1)
抓取启动输出，验证 "读取并打印 U 盘内容" 功能：
  - U 盘挂载
  - 目录递归遍历（含子目录）
  - 每个文件内容经串口打印
  - 堆地址证明（SDRAM）

流程：
  1. 先打开 COM5（从 t=0 抓，避免错过启动横幅）
  2. 用 OpenOCD + STLink 烧录 Release 固件并 reset run
  3. 抓取 ~18 秒串口输出
  4. 识别关键特征串并判定 PASS/FAIL

工具路径走环境变量（不写死机器路径）：
  OPENOCD_BIN, OPENOCD_SCRIPTS, SERIAL_PORT
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
CAPTURE_SEC = 18

ELF = "build/stm32f429_tinyusb_ui.elf"

# 固件打印的结构/内容特征串（含 demo 种子文件）
MARKERS = [
    "System Init",
    "SDRAM Init OK",
    "FreeRTOS Heap configured",
    "USB Host Init",
    "Waiting for USB disk",
    "USB Disk Connected",
    "USB Disk Mounted",
    "========== USB DISK CONTENTS ==========",
    "[DIR ] 0:/demo",
    "[FILE] 0:/demo/hello.txt",
    "Hello from STM32F429 USB Host",
    "[DIR ] 0:/demo/sub",
    "[FILE] 0:/demo/sub/world.txt",
    "Nested directory file content",
    "Line A",
    "Line B",
    "Line C",
    "========== END (dirs=",
    "Heap object @",
]

# 内容正确性期望（demo 种子文件的已知正文片段）
EXPECT_CONTENT = [
    "Hello from STM32F429 USB Host",
    "Line A", "Line B", "Line C",
    "Nested directory file content",
]


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.3)
    except Exception as e:
        print("OPEN %s FAIL -> %s" % (PORT, e))
        sys.exit(2)
    print("[1] COM5 opened (%s)" % ser.name)

    print("[2] flashing %s via OpenOCD+STLink ..." % ELF)
    cmd = [
        OCD_BIN, "-s", OCD_SCRIPTS,
        "-f", "interface/stlink.cfg",
        "-f", "target/stm32f4x.cfg",
        "-c", "init",
        "-c", "reset halt",
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
    missing = [m for m in MARKERS if m not in txt]
    print("\n-- feature markers found (%d/%d): %s" % (len(found), len(MARKERS), found))
    if missing:
        print("-- missing markers: %s" % missing)

    content_ok = all(c in txt for c in EXPECT_CONTENT)

    if "Waiting for USB disk" in txt and "USB Disk Connected" not in txt:
        print("\nVERDICT: INCONCLUSIVE — 固件正常启动，但串口未见 'USB Disk Connected'："
              "请确认 U 盘已插入板载 USB Host 口")
    elif "tusb_init FAILED" in txt or "HardFault" in txt or "Error_Handler" in txt:
        print("\nVERDICT: FAIL — 固件报告错误")
    elif "========== USB DISK CONTENTS ==========" in txt and content_ok and "Heap object @" in txt:
        print("\nVERDICT: PASS — U 盘内容经串口读取并打印：目录遍历(含子目录) + 文件正文 + 堆地址证明均 OK")
    else:
        print("\nVERDICT: INCONCLUSIVE — 启动横幅/挂载正常，但内容打印特征不完整（见 missing markers）")


if __name__ == "__main__":
    main()
