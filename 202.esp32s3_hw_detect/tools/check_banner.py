# Quick serial banner check: pulse RTS reset and capture boot log.
import serial, time, sys
from serial.tools import list_ports

port = None
s = None
ports = list(list_ports.comports())
ports.sort(key=lambda p: 0 if p.vid == 0x303a else 1)  # ESP32 USB-Serial-JTAG first
for p in ports:
    try:
        s = serial.Serial(p.device, 115200, timeout=1)
        port = p.device
        break
    except Exception:
        continue
if not port:
    print("RESULT: FAIL no serial port found"); sys.exit(1)
print("PORT:", port)

s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)
time.sleep(0.1); s.setDTR(True)

deadline = time.time() + 12
buf = b""
while time.time() < deadline:
    buf += s.read(1024)
s.close()
text = buf.decode("utf-8", errors="replace")
print(text[-2500:])

checks = {
    "firmware_banner": "ESP32-S3 Remote Hardware Debugger" in text,
    "ws_ready": "WebSocket: ready :81" in text or "server on port 81" in text,
    "ap_name": "wifi-" in text and "192.168.4.1" in text,
    "system_ready": "System Ready" in text,
}
print("CHECKS:", checks)
print("RESULT:", "PASS" if all(checks.values()) else "PARTIAL/FAIL")
