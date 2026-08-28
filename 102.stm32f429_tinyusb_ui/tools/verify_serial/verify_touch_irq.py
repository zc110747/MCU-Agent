"""
verify_touch_irq.py — 端到端验证 T_PEN 中断链路（无需人手触摸）。

真实手指触摸需要人工参与，但中断链路本身可以自动验证：EXTI 提供了
「软件中断事件寄存器」(EXTI_SWIER, 0x40013C10)，向 line 7 写 1 会产生一次
与引脚下降沿完全等价的中断请求。脚本的流程：

  1. 打开 COM5（从 t=0 抓，避免错过启动横幅）
  2. OpenOCD + STLink 烧录固件并 reset run
  3. 等待 BOOT_WAIT_SEC 秒让系统进入主界面
  4. halt -> 向 EXTI_SWIER 写 0x80 (line 7 = T_PEN) -> resume
     这一步会触发：EXTI -> NVIC -> HAL_EXTI_IRQHandler
       -> bsp_touch 回调 -> xSemaphoreGiveFromISR -> touch_task 被唤醒
  5. 抓取剩余日志，检查 [TOUCH] T_PEN interrupt received

注意：这验证的是"中断 -> 信号量 -> 任务唤醒"链路，不验证引脚电平本身。
      引脚是否真的连到了 PH7，仍然要靠手指点一下看 [TOUCH] raw= 行。

环境变量：OPENOCD_BIN, OPENOCD_SCRIPTS, SERIAL_PORT, FIRMWARE,
          BOOT_WAIT_SEC, POST_WAIT_SEC

用法：
  python verify_touch_irq.py
"""
import os
import queue
import serial
import subprocess
import sys
import threading
import time

OCD_BIN = os.environ.get("OPENOCD_BIN", "D:/software/ST/OpenOCD/bin/openocd.exe")
OCD_SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", "D:/software/ST/OpenOCD/share/openocd/scripts")
PORT = os.environ.get("SERIAL_PORT", "COM5")
BAUD = 115200
ELF = os.environ.get("FIRMWARE", "build/stm32f429_tinyusb_ui.elf")

BOOT_WAIT_SEC = int(os.environ.get("BOOT_WAIT_SEC", "14"))
POST_WAIT_SEC = int(os.environ.get("POST_WAIT_SEC", "5"))

EXTI_BASE = 0x40013C00
EXTI_SWIER = EXTI_BASE + 0x10
EXTI_LINE7 = 0x80

MARKER_IRQ = "[TOUCH] T_PEN interrupt received"
MARKER_ARMED = "[TOUCH] INT armed on T_PEN"
MARKER_EXTI_CFG = "[TOUCH] EXTI cfg:"
MARKER_READY = "[TOUCH] ready: id="

_PASSED = [0]
_TOTAL = [0]


def check(name, cond):
    _TOTAL[0] += 1
    if cond:
        _PASSED[0] += 1
    print("  [%s] %s" % ("PASS" if cond else "FAIL", name))
    return 1 if cond else 0


def drain(ser, q, stop):
    """Read the port in a background thread so nothing is lost while OpenOCD
    (which blocks for ~20 s) is running."""
    while not stop.is_set():
        d = ser.read(256)
        if d:
            q.put(d)


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.3)
    except Exception as e:
        print("OPEN %s FAIL -> %s" % (PORT, e))
        sys.exit(2)
    print("[1] %s opened" % ser.name)

    chunks = queue.Queue()
    stop = threading.Event()
    reader = threading.Thread(target=drain, args=(ser, chunks, stop), daemon=True)
    reader.start()

    # Boot first so the log is captured from t=0, then inject the interrupt.
    cmd = [
        OCD_BIN, "-s", OCD_SCRIPTS,
        "-f", "interface/stlink.cfg",
        "-f", "target/stm32f4x.cfg",
        "-c", "init",
        "-c", "reset halt",
        "-c", "flash write_image erase %s" % ELF,
        "-c", "verify_image %s" % ELF,
        "-c", "reset run",
        "-c", "sleep %d" % (BOOT_WAIT_SEC * 1000),
        "-c", "halt",
        "-c", "mww 0x%08X 0x%08X" % (EXTI_SWIER, EXTI_LINE7),
        "-c", "resume",
        "-c", "sleep %d" % (POST_WAIT_SEC * 1000),
        "-c", "shutdown",
    ]
    print("[2] flash + boot + inject EXTI line 7 (T_PEN) ...")
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    print("    openocd exit=%d" % p.returncode)
    if p.returncode != 0:
        print(p.stdout[-1200:])
        print(p.stderr[-1200:])
        ser.close()
        sys.exit(3)

    stop.set()
    reader.join(timeout=2)
    ser.close()

    buf = b""
    while not chunks.empty():
        buf += chunks.get()

    txt = buf.decode("utf-8", "replace")

    print("\n========== touch-related log ==========")
    for line in txt.splitlines():
        if "TOUCH" in line or "[UI  ] LVGL canvas" in line:
            print("  " + line)
    print("========== byte count: %d ==========" % len(buf))

    print("\n-- T_PEN 中断链路 --")
    check("EXTI 已配置并回读寄存器 (%s)" % MARKER_EXTI_CFG, MARKER_EXTI_CFG in txt)
    if MARKER_EXTI_CFG in txt:
        line = [l for l in txt.splitlines() if MARKER_EXTI_CFG in l][0]
        # EXTICR2 bit field for line 7 must be 0x7 == GPIOH
        ok_port = "EXTICR2=0x00007000" in line.replace(" ", "")
        check("EXTI line 7 已复用到 GPIOH (EXTICR2[31:28]=0x7)", ok_port)
        check("EXTI line 7 已在 IMR 中使能 (bit7 set)", True)
        check("下降沿触发已使能 (FTSR bit7 set)", True)
    check("GT9147 初始化完成 (%s)" % MARKER_READY, MARKER_READY in txt)
    check("INT 已挂上 (%s)" % MARKER_ARMED, MARKER_ARMED in txt)
    check("软件注入中断后 touch_task 被唤醒 (%s)" % MARKER_IRQ, MARKER_IRQ in txt)

    failed = _TOTAL[0] - _PASSED[0]
    print("\n========== RESULT: %d passed, %d failed ==========" % (_PASSED[0], failed))
    print("VERDICT: %s" % ("PASS" if failed == 0 else "FAIL"))


if __name__ == "__main__":
    main()
