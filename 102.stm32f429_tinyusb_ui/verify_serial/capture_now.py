import serial, time, subprocess, sys

PORT = 'COM5'
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1.0)
ser.reset_input_buffer()

# Trigger a board reset via OpenOCD (re-init + reset run), then let it boot.
openocd = [
    'D:/software/ST/OpenOCD/bin/openocd.exe',
    '-s', 'D:/software/ST/OpenOCD/share/openocd/scripts',
    '-f', 'interface/stlink.cfg',
    '-f', 'target/stm32f4x.cfg',
    '-c', 'init',
    '-c', 'reset run',
    '-c', 'shutdown',
]
subprocess.run(openocd, capture_output=True, text=True, timeout=60)

t0 = time.time()
lines = []
markers = ('System Init', 'LCD', 'GRAM', 'FONT', 'USB Disk', 'USB Host',
           'Waiting', 'Mounted', 'error', 'Error')
while time.time() - t0 < 18.0:
    raw = ser.readline()
    if not raw:
        continue
    s = raw.decode('utf-8', 'replace').rstrip('\r\n')
    if not s:
        continue
    lines.append(s)
    if any(m in s for m in markers):
        print('>>', s)
ser.close()
print('--- total lines captured:', len(lines))
sys.exit(0)
