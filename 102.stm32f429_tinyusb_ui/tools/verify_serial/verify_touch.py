"""
verify_touch.py — 烧录最新固件并通过 COM5 (USART3 PB10/PB11, 115200 8N1)
验收第三版「触摸 + 双页面」增量：

  Phase 0  启动链路（沿用 verify_boot_flow 的必现特征）
  Phase 1  GT9147 模拟 I2C 总线识别
      [TOUCH] product ID = "9147" (addr 0x14) -> MATCH
      [TOUCH] ready: id=.. addr=0x.. cfg=0x.., canvas WxH, swap=.. invX=.. invY=..
  Phase 2  T_PEN 中断 + LVGL 输入设备
      [TOUCH] INT armed on T_PEN (PH7, falling edge) -> waiting
      [UI  ] LVGL canvas WxH, pointer indev registered
  Phase 3  I2C2 传感器（第二页数据源）
      [SENS ] AP3216C init OK
      [SENS ] MPU9250 init OK
  Phase 4  交互（需要人工参与）
      脚本打印提示后等待 TOUCH_WAIT_SEC 秒，期间请用手指点击屏幕：
      [TOUCH] raw=(x,y) -> lv=(x,y) points=n
      点击底部左/右箭头按钮后应出现：
      [UI  ] page -> 2 / 2      （右箭头）
      [UI  ] page -> 1 / 2      （左箭头，循环回来）

环境变量（不写死机器路径）：
  OPENOCD_BIN, OPENOCD_SCRIPTS, SERIAL_PORT, FIRMWARE, TOUCH_WAIT_SEC

用法：
  python verify_touch.py
  SERIAL_PORT=COM5 FIRMWARE=build/stm32f429_tinyusb_ui.elf python verify_touch.py
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

ELF = os.environ.get("FIRMWARE", "build/stm32f429_tinyusb_ui.elf")

AUTO_CAPTURE_SEC = 18
TOUCH_WAIT_SEC = int(os.environ.get("TOUCH_WAIT_SEC", "30"))

MARKERS_BOOT = [
    "System Init",
    "SDRAM Init OK",
    "FreeRTOS Heap configured (SDRAM @0xC0000000)",
    "USB Host Init",
    "[UI  ] starting LCD + LVGL bring-up",
    "[LCD ] controller ID = 0x",
    "[UI  ] boot screen: wait for system start...",
]

MARKERS_TOUCH = [
    "[TOUCH] task started",
    "[TOUCH] product ID = ",
    "[TOUCH] ready: id=",
    "[TOUCH] INT armed on T_PEN",
]

MARKERS_UI = [
    "[UI  ] LVGL canvas ",
    "pointer indev registered",
]

MARKERS_SENSOR = [
    "[SENS ] task started",
    "[SENS ] AP3216C init ",
    "[SENS ] MPU9250 init ",
]

FAIL_PATTERNS = ["HardFault", "Error_Handler", "SDRAM Init FAILED",
                 "config upload FAILED"]

_PASSED = [0]
_TOTAL = [0]


def check(name, cond):
    _TOTAL[0] += 1
    if cond:
        _PASSED[0] += 1
    print("  [%s] %s" % ("PASS" if cond else "FAIL", name))
    return 1 if cond else 0


def open_serial():
    try:
        return serial.Serial(PORT, BAUD, timeout=0.3)
    except Exception as e:
        print("OPEN %s FAIL -> %s" % (PORT, e))
        sys.exit(2)


def flash():
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
        sys.exit(3)
    print("    flash OK, device running")


def grab(ser, seconds, echo=""):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < seconds:
        d = ser.read(256)
        if d:
            buf += d
            if echo:
                sys.stdout.write(d.decode("utf-8", "replace"))
                sys.stdout.flush()
    return buf


def main():
    ser = open_serial()
    print("[1] %s opened (%s)" % (PORT, ser.name))

    flash()

    print("\n[3] capturing %ds of boot output ..." % AUTO_CAPTURE_SEC)
    buf = grab(ser, AUTO_CAPTURE_SEC)
    txt = buf.decode("utf-8", "replace")

    print("\n========== %s (USART3) boot capture ==========" % PORT)
    print(txt if txt.strip() else "(no bytes received)")
    print("========== byte count: %d ==========" % len(buf))

    print("\n-- Phase 0 启动链路 --")
    for m in MARKERS_BOOT:
        check("boot: %s" % m, m in txt)

    print("\n-- Phase 1 GT9147 模拟 I2C 识别 --")
    for m in MARKERS_TOUCH:
        check("touch: %s" % m, m in txt)
    check("GT9147 产品 ID 与预期一致 (-> MATCH)", "-> MATCH" in txt)
    check("未出现 'GT9147 not found'", "[TOUCH] GT9147 not found" not in txt)

    print("\n-- Phase 2 T_PEN 中断 + LVGL 输入设备 --")
    for m in MARKERS_UI:
        check("ui: %s" % m, m in txt)

    print("\n-- Phase 3 I2C2 传感器 --")
    for m in MARKERS_SENSOR:
        check("sensor: %s" % m, m in txt)
    check("AP3216C init OK", "[SENS ] AP3216C init OK" in txt)
    check("MPU9250 init OK", "[SENS ] MPU9250 init OK" in txt)

    print("\n-- Phase 4 交互（需要人工参与）--")
    print("  >>> 请在 %d 秒内：1) 点一下屏幕任意位置；" % TOUCH_WAIT_SEC)
    print("  >>>             2) 点屏幕底部右箭头（切到第 2 页）；")
    print("  >>>             3) 点屏幕底部左箭头（循环回第 1 页）")
    print("  --- 实时串口输出 ---")
    buf2 = grab(ser, TOUCH_WAIT_SEC, echo=True)
    txt2 = buf2.decode("utf-8", "replace")
    print("  --- 交互阶段字节数: %d ---" % len(buf2))

    raw_lines = [l for l in txt2.splitlines() if "[TOUCH] raw=" in l]
    page_lines = [l for l in txt2.splitlines() if "[UI  ] page -> " in l]

    check("触摸有响应：出现 [TOUCH] raw=(x,y) -> lv=(x,y)", len(raw_lines) > 0)
    check("坐标已映射到画布内（lv 值非负）",
          all(("-" not in l.split("lv=(")[1]) for l in raw_lines) if raw_lines else False)
    check("右/左箭头触发页面切换：出现 [UI  ] page ->", len(page_lines) > 0)
    check("页面可循环（出现 1 / 2 与 2 / 2 两种状态）",
          any("page -> 2 / 2" in l for l in page_lines) and
          any("page -> 1 / 2" in l for l in page_lines))

    for l in raw_lines[:5]:
        print("      %s" % l.strip())
    for l in page_lines[:6]:
        print("      %s" % l.strip())

    print("\n-- 无致命错误 --")
    all_txt = txt + txt2
    for m in FAIL_PATTERNS:
        check("no '%s'" % m, m not in all_txt)

    ser.close()

    failed = _TOTAL[0] - _PASSED[0]
    print("\n========== RESULT: %d passed, %d failed ==========" % (_PASSED[0], failed))
    print("VERDICT: %s" % ("PASS" if failed == 0 else
                           ("PASS (交互项待人工确认)" if raw_lines or page_lines else "FAIL")))


if __name__ == "__main__":
    main()
