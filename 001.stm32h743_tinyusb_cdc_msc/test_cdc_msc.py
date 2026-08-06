import serial, time, sys

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"

s = serial.Serial(PORT, 115200, timeout=0.5, dsrdtr=True)
time.sleep(0.3)
s.dtr = True
time.sleep(0.4)

def drain(t=0.7):
    time.sleep(t)
    n = s.in_waiting or 1
    return s.read(n).decode(errors="replace")

print("=== BANNER ===")
print(drain(0.7))

for cmd in ["sd", "ls", "clk", "stats", "help"]:
    s.write((cmd + "\r").encode())
    out = drain(0.8)
    print(f"=== {cmd} ===")
    print(out)

s.close()
