#!/usr/bin/env python3
"""Flash official camera.hex from a subprocess while serial is ALREADY open,
so nothing printed at boot is missed."""
import subprocess
import sys
import threading
import time
import serial

FLASH = r"D:/software/Python3/python.exe"
SCRIPT = r"E:/cnb/git/MCU-Agent/301.ra8d1_lcd_lvgl/tools/flash/flash.py"
HEX = r"E:/cnb/git/sdk-bsp-ra8d1-vision-board-master/projects/vision_board_camera/firmware/camera.hex"

s = serial.Serial("COM9", 115200, timeout=0.1)
out = bytearray()
stop = False

def reader():
    while not stop:
        try:
            out.extend(s.read(4096))
        except Exception:
            break

th = threading.Thread(target=reader)
th.start()

subprocess.run([FLASH, SCRIPT, HEX], capture_output=True)
print("flash done, capturing 12 s from reset...")
t0 = time.time()
while time.time() - t0 < 12.0:
    time.sleep(0.2)
stop = True
th.join()

text = out.decode(errors="replace")
print(text[:2500])
print("=====")
print("has demo banner:", "camera display demo" in text)
print("sensor-not-found:", "not found" in text.lower())
s.close()
