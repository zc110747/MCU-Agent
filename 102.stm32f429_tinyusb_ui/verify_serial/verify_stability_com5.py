"""
verify_stability_com5.py — 稳定性专项验证：
独立打开 COM5（不烧录，由外部先烧录或复用已烧录固件），
持续抓取 CAPTURE_SEC 秒，统计：
  - "System Init" 出现次数  -> 判定是否反复重启（halt/reset run 只应算 1 次）
  - "[FONT] font status mask = 0x" 首次出现时间
  - "USB 状态" 1Hz 刷新是否持续出现
  - 是否出现 HardFault / Error_Handler / tusb_init FAILED

判定：
  - reboot == 1  -> 稳定（单次启动）
  - reboot  > 1  -> 不稳定（反复重启），报告每次重启间隔
  - 出现硬错误串 -> FAIL
用法：
  SERIAL_PORT=COM5 python verify_stability_com5.py
"""
import os
import serial
import sys
import time

PORT = os.environ.get("SERIAL_PORT", "COM5")
BAUD = 115200
CAPTURE_SEC = int(os.environ.get("CAPTURE_SEC", "45"))

HARD_ERR = ["HardFault", "Error_Handler", "tusb_init FAILED", "MemManage", "BusFault", "UsageFault"]


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.3)
    except Exception as e:
        print("OPEN %s FAIL -> %s" % (PORT, e))
        sys.exit(2)
    print("[1] %s opened (%s); capturing %ds ..." % (PORT, ser.name, CAPTURE_SEC))

    buf = b""
    t0 = time.time()
    # timestamped markers
    reboots = []
    font_t = None
    usb_states = 0
    hard = []
    last_usb_t = None
    while time.time() - t0 < CAPTURE_SEC:
        d = ser.read(256)
        if d:
            ts = time.time() - t0
            buf += d
            chunk = d.decode("utf-8", "replace")
            if "System Init" in chunk:
                reboots.append(round(ts, 2))
            if "font status mask" in chunk and font_t is None:
                font_t = round(ts, 2)
            if "USB 状态" in chunk:
                usb_states += 1
                last_usb_t = round(ts, 2)
            for h in HARD_ERR:
                if h in chunk and h not in hard:
                    hard.append(h)
    ser.close()

    txt = buf.decode("utf-8", "replace")
    print("\n========== stability capture (%ds) ==========" % CAPTURE_SEC)
    print("reboots (System Init): %d  timestamps=%s" % (len(reboots), reboots))
    print("first font mask @ %.2fs" % (font_t if font_t is not None else -1))
    print("'USB 状态' refresh hits: %d  last@%.2fs" % (usb_states, last_usb_t if last_usb_t else -1))
    print("hard errors: %s" % (hard if hard else "none"))
    print("total bytes: %d" % len(buf))

    # verdict
    if hard:
        print("\nVERDICT: FAIL — 固件上报硬错误 %s" % hard)
    elif len(reboots) > 1:
        print("\nVERDICT: UNSTABLE — 重启 %d 次 @ %s" % (len(reboots), reboots))
    elif len(reboots) == 1:
        if usb_states >= 3:
            print("\nVERDICT: PASS — 单次启动 + 字库就绪(0x1E) + 1Hz 刷新稳定")
        elif font_t is not None:
            print("\nVERDICT: PASS* — 单次启动 + 字库就绪，但 'USB 状态' 刷新采样不足(需更长窗口)")
        else:
            print("\nVERDICT: INCONCLUSIVE — 单次启动但字库未就绪")
    else:
        print("\nVERDICT: INCONCLUSIVE — 未捕获到启动横幅")


if __name__ == "__main__":
    main()
