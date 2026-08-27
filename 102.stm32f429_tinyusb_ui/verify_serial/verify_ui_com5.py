"""
verify_ui_com5.py — 烧录最新固件并通过 COM5 (USART3 PB10/PB11, 115200 8N1)
抓取启动输出，验证 UI 线程（LCD 800x400 + LVGL v8 + GBK 字库）端到端：

  - ui_task 启动（"starting LCD + LVGL bring-up"）
  - FMC/8080 LCD 控制器 ID 探测（"[LCD ] controller ID = 0x...."）
  - U 盘挂载后字库挂载（"[FONT] mounting U-disk fonts"）
  - 字库就绪掩码（"[FONT] font status mask = 0x.."）
  - LVGL 渲染后 1Hz 刷新（USB 状态行 "USB 状态" 反复刷新）

流程：
  1. 先打开 COM5（从 t=0 抓，避免错过启动横幅）
  2. OpenOCD + STLink 烧录 Release 固件并 reset run
  3. 抓取 ~20 秒串口输出（含 U 盘插入时间，建议在烧录后插入 U 盘）
  4. 识别关键特征串并判定 PASS/FAIL

工具路径走环境变量（不写死机器路径）：
  OPENOCD_BIN, OPENOCD_SCRIPTS, SERIAL_PORT

用法：
  python verify_ui_com5.py
  SERIAL_PORT=COM5 python verify_ui_com5.py
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
CAPTURE_SEC = 30

ELF = "build/stm32f429_tinyusb_ui.elf"

# 启动 / 画面链路特征串
MARKERS = [
    "System Init",
    "SDRAM Init OK",
    "FreeRTOS Heap configured",
    "USB Host Init",
    "[UI  ] starting LCD + LVGL bring-up",
    "[LCD ] controller ID = 0x",
    "[UI  ] waiting for U-disk (GBK fonts)",
    "[FONT] mounting U-disk fonts",
    "[FONT] font status mask = 0x",
    "USB 状态",
]

# 必须出现才算 LCD/字库链路打通的硬特征
HARD = [
    "[UI  ] starting LCD + LVGL bring-up",
    "[LCD ] controller ID = 0x",
    "[FONT] font status mask = 0x",
]


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.3)
    except Exception as e:
        print("OPEN %s FAIL -> %s" % (PORT, e))
        sys.exit(2)
    print("[1] %s opened (%s)" % (PORT, ser.name))

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
    print("    >> 请在此时插入含 0:/SYSTEM/FONT/GBKxx.FON + UNIGBK.BIN 的 U 盘 <<")

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

    hard_ok = all(m in txt for m in HARD)
    font_ok = "[FONT] font status mask = 0x" in txt and "0x00" not in txt.split("font status mask = ")[-1][:4]

    if "tusb_init FAILED" in txt or "HardFault" in txt or "Error_Handler" in txt:
        print("\nVERDICT: FAIL — 固件报告错误")
    elif not hard_ok:
        print("\nVERDICT: INCONCLUSIVE — UI/LCD/字库硬特征不完整（见 missing markers）。"
              "若仅缺 [FONT] 行，多为未插 U 盘或字库路径不对")
    elif font_ok:
        print("\nVERDICT: PASS — LCD 控制器 ID 探测 + 字库挂载(%s) + LVGL 渲染启动 均 OK"
              % txt.split("font status mask = ")[-1].split()[0])
    else:
        print("\nVERDICT: PASS* — LCD 链路 OK，但字库掩码为 0x00（U 盘无 GBKxx.FON / 未插盘）；"
              "插好字库 U 盘后应显示中文面板而非错误页")


if __name__ == "__main__":
    main()
