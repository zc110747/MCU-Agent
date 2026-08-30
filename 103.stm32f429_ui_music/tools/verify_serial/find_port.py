#!/usr/bin/env python3
"""Find which COM port carries the USART3 debug console by rebooting the board
and capturing both CH340 ports. Usage: find_port.py COM4 COM7
"""
import sys, time, socket, serial

PORTS = sys.argv[1:] or ["COM4", "COM7"]
TELNET = ("127.0.0.1", 4444)

def telnet_send(cmd):
    try:
        s = socket.create_connection(TELNET, timeout=5)
        s.sendall((cmd + "\n").encode())
        time.sleep(0.5)
        s.close()
    except Exception as e:
        print(f"[TELNET] send failed: {e}")

def main():
    print(f"[FIND] opening {PORTS}")
    sers = {}
    for p in PORTS:
        try:
            sers[p] = serial.Serial(p, 115200, timeout=0.1)
            sers[p].reset_input_buffer()
            print(f"[FIND] {p} opened")
        except Exception as e:
            print(f"[FIND] {p} open failed: {e}")

    # reboot board so it prints the boot banner
    print("[FIND] sending 'reset run' via openocd telnet")
    telnet_send("reset run")

    t0 = time.time()
    buf = {p: [] for p in sers}
    while time.time() - t0 < 7.0:
        for p, ser in sers.items():
            try:
                line = ser.readline().decode(errors="replace").rstrip("\r\n")
                if line:
                    buf[p].append(line)
                    print(f"[{p}] {line}")
            except Exception:
                pass

    for p, ser in sers.items():
        ser.close()
        print(f"[FIND] {p}: {len(buf[p])} lines captured")

if __name__ == "__main__":
    main()
