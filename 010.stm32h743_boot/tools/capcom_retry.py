import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM19"
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 30
out = sys.argv[3] if len(sys.argv) > 3 else "log/capcom.txt"

ser = None
for attempt in range(20):
    try:
        ser = serial.Serial(port, 115200, timeout=0.5, exclusive=False)
        break
    except Exception as e:
        time.sleep(0.3)
if ser is None:
    print("FAILED to open %s" % port)
    sys.exit(2)

t0 = time.time()
buf = []
try:
    while time.time() - t0 < secs:
        try:
            b = ser.read(512)
        except Exception as e:
            buf.append(b"[ERR] " + str(e).encode())
            break
        if b:
            buf.append(b)
    ser.close()
except KeyboardInterrupt:
    pass

with open(out, "wb") as f:
    for chunk in buf:
        f.write(chunk)
print("captured %d bytes -> %s" % (sum(len(c) for c in buf), out))
