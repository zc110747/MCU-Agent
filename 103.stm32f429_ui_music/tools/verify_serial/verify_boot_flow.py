"""
verify_boot_flow.py — 烧录最新固件并通过 COM5 (USART3 PB10/PB11, 115200 8N1)
抓取启动输出，验证「启动加载状态机」端到端：

  Phase 0  裸机启动
      System Init / I2C / SDRAM Init OK / FreeRTOS Heap configured
  Phase 1  RTOS 起来后 USB 主机初始化（必须晚于调度器）
      USB Host Init
  Phase 2  LVGL 启动页（纯 ASCII，内部字表，不依赖任何文件）
      [UI  ] boot screen: wait for system start...
  Phase 3  字库加载：先 microSD(1:)，失败再等 USB(0:)
      [SD  ] SDIO init OK ...       或 [SD  ] SDIO init FAILED (no card?)
      [FONT] trying microSD 1:/SYSTEM/FONT/   或  [FONT] trying USB 0:/SYSTEM/FONT/
      [FONT] source=1: mask=0x..   或  source=0: mask=0x..
      [UI  ] main screen (fonts from 1:/0:)
  Phase 3' 10s 内都没有字库
      [UI  ] timeout: sdcard and usb loader failed!

流程：
  1. 先打开 COM5（从 t=0 抓，避免错过启动横幅）
  2. OpenOCD + STLink 烧录固件并 reset run
  3. 抓取 ~25 秒串口输出
  4. 识别关键特征串，输出 pass/fail 计数与最终 VERDICT

工具路径走环境变量（不写死机器路径）：
  OPENOCD_BIN, OPENOCD_SCRIPTS, SERIAL_PORT, FIRMWARE

用法：
  python verify_boot_flow.py
  SERIAL_PORT=COM5 FIRMWARE=build_rel/stm32f429_tinyusb_ui.elf python verify_boot_flow.py
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
CAPTURE_SEC = 25

ELF = os.environ.get("FIRMWARE", "build_rel/stm32f429_tinyusb_ui.elf")

# ---- 必现特征（无论有没有插卡/插 U 盘都必须出现）-------------------------
MARKERS_BOOT = [
    "System Init",
    "SDRAM Init OK",
    "FreeRTOS Heap configured (SDRAM @0xC0000000)",
    "USB Host Init",
    "[UI  ] starting LCD + LVGL bring-up",
    "[LCD ] controller ID = 0x",
    "[UI  ] boot screen: wait for system start...",
]

# ---- 二选一结局：主界面 或 超时失败页 ------------------------------------
MARKER_MAIN_SD = "[UI  ] main screen (fonts from 1:)"
MARKER_MAIN_USB = "[UI  ] main screen (fonts from 0:)"
MARKER_TIMEOUT = "[UI  ] timeout: sdcard and usb loader failed!"

# ---- 存储介质探测（信息性）------------------------------------------------
MARKER_SD_OK = "[SD  ] SDIO init OK"
MARKER_SD_FAIL = "[SD  ] SDIO init FAILED"
MARKER_SD_MOUNT = "[SD  ] mounted 1:"

FAIL_PATTERNS = ["tusb_init FAILED", "HardFault", "Error_Handler", "SDRAM Init FAILED"]


def check(name, cond):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", name))
    return 1 if cond else 0


def main():
    passed = 0
    total = 0

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
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
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
    print("\n========== %s (USART3) capture ==========" % PORT)
    print(txt if txt.strip() else "(no bytes received)")
    print("========== byte count: %d ==========" % len(buf))

    print("\n-- Phase 0/1/2 启动链路 --")
    for m in MARKERS_BOOT:
        total += 1
        passed += check("boot: %s" % m, m in txt)

    print("\n-- Phase 3 存储介质 --")
    sd_ok = MARKER_SD_OK in txt
    if sd_ok:
        total += 1
        passed += check("microSD SDIO init OK", True)
        total += 1
        passed += check("microSD mounted 1:", MARKER_SD_MOUNT in txt)
    else:
        total += 1
        passed += check("microSD absent -> graceful fallback (SDIO init FAILED logged)",
                        MARKER_SD_FAIL in txt)

    print("\n-- Phase 3/3' 结局（二选一）--")
    main_sd = MARKER_MAIN_SD in txt
    main_usb = MARKER_MAIN_USB in txt
    timed_out = MARKER_TIMEOUT in txt
    total += 1
    passed += check("结局唯一：主界面(SD/USB) 或 10s 超时失败页",
                    (main_sd or main_usb or timed_out) and
                    not ((main_sd or main_usb) and timed_out))

    if main_sd or main_usb:
        total += 1
        passed += check("字库加载成功并进入主界面 (source=%s)" %
                        ("SD 1:" if main_sd else "USB 0:"), True)
        total += 1
        passed += check("[FONT] source= 行存在",
                        "[FONT] source=" in txt)

        # CJK 渲染证据：字形缓存 miss>0 说明中文字形真的从字库文件读出来了
        misses = None
        for line in txt.splitlines():
            if "glyph cache: hits=" in line:
                try:
                    misses = int(line.split("misses=")[1].split()[0])
                except (IndexError, ValueError):
                    misses = None
        total += 1
        passed += check("[UI  ] glyph cache 行存在", misses is not None)
        total += 1
        passed += check("CJK 字形确实从字库读出 (misses>0，实测 %s)" % misses,
                        misses is not None and misses > 0)
    elif timed_out:
        total += 1
        passed += check("10s 超时居中显示 'sdcard and usb loader failed!'", True)
        total += 1
        passed += check("超时后未误报主界面", not (main_sd or main_usb))

    print("\n-- 无致命错误 --")
    for m in FAIL_PATTERNS:
        total += 1
        passed += check("no '%s'" % m, m not in txt)

    print("\n========== RESULT: %d passed, %d failed ==========" % (passed, total - passed))
    if total - passed == 0:
        print("VERDICT: PASS")
    else:
        print("VERDICT: FAIL")


if __name__ == "__main__":
    main()
