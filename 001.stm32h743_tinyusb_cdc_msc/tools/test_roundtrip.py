# Round-trip proof: host writes a file to the USB U-disk (G:), then the
# device (FatFs over CDC) reads it back. Confirms CDC + MSC coexist on one SD
# card with no interference.
import os, time, serial, sys

PAYLOAD = "RT-2026-08-05-ROUNDTRIP-OK-7F3A9C"
HOST_PATH = "G:/rt.txt"
COM = "COM7"
BAUD = 115200

def cdc(cmd, hold=0.8):
    s.write((cmd + "\r").encode())
    time.sleep(hold)
    data = b""
    # drain
    t0 = time.time()
    while time.time() - t0 < hold + 0.5:
        n = s.in_waiting
        if n:
            data += s.read(n)
        else:
            time.sleep(0.05)
    return data.decode(errors="replace")

# ---- 1. HOST: write the marker file and flush to physical media -----------
print("[host] writing", HOST_PATH)
if not os.path.isdir("G:/"):
    print("[host] G: is NOT mounted - cannot write. Abort.")
    sys.exit(2)
with open(HOST_PATH, "wb") as f:
    f.write(PAYLOAD.encode())
    f.flush()
    os.fsync(f.fileno())          # FlushFileBuffers -> pushes to the SD card
print("[host] wrote payload:", PAYLOAD)

# ---- 2. DEVICE: open CDC and exercise it while the U-disk is mounted ------
print("[device] opening", COM)
s = serial.Serial(COM, BAUD, timeout=0.5, dsrdtr=True)
time.sleep(0.3); s.dtr = True; time.sleep(0.4)
s.read(s.in_waiting or 1)         # clear banner

# show CDC is alive & host owns the card (concurrency check)
print("--- 'sd' (CDC) ---")
print(cdc("sd", 0.6))
print("--- 'remount' (re-read on-disk FAT) ---")
print(cdc("remount", 0.8))
print("--- 'cat rt.txt' (read host-written file from device side) ---")
out = cdc("cat rt.txt", 1.0)
print(out)

# ---- 3. Verify -------------------------------------------------------------
if PAYLOAD in out:
    print("\n[RESULT] PASS - host U-disk write is visible to device FatFs.")
    rc = 0
else:
    print("\n[RESULT] FAIL - payload not seen on device side.")
    rc = 1
s.close()
sys.exit(rc)
