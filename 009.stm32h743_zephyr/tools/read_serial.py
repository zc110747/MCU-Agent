import serial, subprocess, time, sys

OPENOCD = r"D:\Software\openocd\bin\openocd.exe"
SCRIPTS = r"D:\Software\openocd\share\openocd\scripts"
PORT = "COM19"
BAUD = 115200

# 1. Open serial first (before reset so we catch the boot banner)
try:
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
except Exception as e:
    print("SERIAL_OPEN_FAIL:", repr(e))
    sys.exit(1)

time.sleep(0.4)
ser.reset_input_buffer()

# 2. Reset & run the target via OpenOCD
cmd = [OPENOCD, "-s", SCRIPTS, "-f", "interface/stlink.cfg",
       "-c", "transport select swd", "-f", "target/stm32h7x.cfg",
       "-c", "adapter speed 4000", "-c", "init",
       "-c", "reset run", "-c", "shutdown"]
subprocess.run(cmd, capture_output=True, text=True, timeout=40)

# 3. Read serial for a few seconds
end = time.time() + 8
buf = b""
while time.time() < end:
    data = ser.read(4096)
    if data:
        buf += data
    time.sleep(0.05)

print("=== SERIAL OUTPUT (COM19 @115200) ===")
print(buf.decode("utf-8", errors="replace"))
ser.close()
