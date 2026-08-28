"""
serial_test.py - 识别 STM32F429 USART3(PB10/PB11) 对应的 PC COM 口并捕获启动日志。

流程：
  1. 打开候选 COM 口（默认 COM4/COM5，CH340）。
  2. 复位目标（OpenOCD reset run），重新触发固件启动打印。
  3. 监听 ~12s，按特征串判定哪一个口是 USART3。

固件启动特征串（app/main.c, app/usb_host_app.c）：
  "System Init" / "SDRAM Init OK" / "USB Host Init" / "Waiting for USB disk..."
  U盘插入后："USB Disk Connected (MSC ready)" -> "USB Disk Mounted"
  -> "Create test.txt" -> "Write test.txt" -> "Read test.txt" -> 回显 -> "USB Disk Ready"
"""
import serial
import threading
import subprocess
import time
import sys

OCD  = r"D:/software/ST/OpenOCD/bin/openocd.exe"
SCR  = r"D:/software/ST/OpenOCD/share/openocd/scripts"
PORTS = ["COM4", "COM5"]
BAUD  = 115200
READ_SEC = 15

MARKERS = [
    "System Init", "SDRAM Init", "USB Host Init", "Waiting for USB disk",
    "USB Disk Connected", "USB Disk Mounted", "USB Disk Ready",
    "test.txt", "exFAT", "Heap object",
]

def reader(ser, buf, stop):
    while not stop.is_set():
        try:
            n = ser.in_waiting
            if n:
                buf.extend(ser.read(n))
        except Exception as e:
            buf.extend(("[ERR %s]" % e).encode("utf-8", "replace"))
            break
        time.sleep(0.02)

def reset_target():
    try:
        r = subprocess.run(
            [OCD, "-s", SCR,
             "-f", "interface/stlink.cfg",
             "-f", "target/stm32f4x.cfg",
             "-c", "init", "-c", "reset run", "-c", "shutdown"],
            capture_output=True, text=True, timeout=40)
        print("   [reset] returncode=%d" % r.returncode)
        if r.returncode != 0:
            print("   [reset] stderr:", r.stderr.strip()[:300])
    except Exception as e:
        print("   [reset] exception:", e)

def main():
    serials, bufs, stops, threads = {}, {}, {}, {}
    for p in PORTS:
        try:
            s = serial.Serial(p, BAUD, timeout=0.2)
            s.flushInput()
            serials[p] = s
            bufs[p] = bytearray()
            stops[p] = threading.Event()
            t = threading.Thread(target=reader, args=(s, bufs[p], stops[p]))
            t.daemon = True
            t.start()
            threads[p] = t
            print("[ok] opened", p)
        except Exception as e:
            print("[skip] cannot open", p, "->", e)

    if not serials:
        print("ERROR: no serial port opened. 检查 CH340 是否已插入且驱动正常。")
        sys.exit(2)

    time.sleep(0.5)
    print(">> resetting target to capture boot banner ...")
    reset_target()
    print(">> listening %ds ..." % READ_SEC)
    time.sleep(READ_SEC)

    for p in serials:
        stops[p].set()
        try:
            serials[p].close()
        except Exception:
            pass

    usart3 = None
    for p in PORTS:
        if p not in bufs:
            continue
        text = bufs[p].decode("utf-8", "replace")
        raw = bytes(bufs[p])
        print("\n========== %s ==========" % p)
        print("bytes received: %d" % len(raw))
        if raw:
            print("RAW HEX[:96]:", raw[:96].hex(" "))
        print("DECODED[:3000]:")
        print(text[:3000])
        found = [m for m in MARKERS if m in text]
        print("-- markers:", found)
        if found and usart3 is None:
            usart3 = p

    print("\nIDENTIFIED USART3 PORT:", usart3 if usart3 else "NONE (未发现启动特征串)")
    return 0 if usart3 else 1

if __name__ == "__main__":
    sys.exit(main())
