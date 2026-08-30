#!/usr/bin/env python3
"""Hardware playback smoke test: capture boot log, send p/n/v, confirm alive.

Usage: monitor_play.py COM7
"""
import sys
import time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
BAUD = 115200

def now():
    return time.strftime("%H:%M:%S")

def main():
    print(f"[MON] opening {PORT} @ {BAUD}")
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
    time.sleep(0.3)
    ser.reset_input_buffer()

    cmds = [('p', 5.0), ('n', 3.5), ('v', 3.5)]
    sent = []
    t0 = time.time()
    last_cmd = None
    cmd_idx = 0
    next_send = time.time() + 3.5   # wait for boot first

    saw = {"screen_up": False, "cmd": [], "decode": False, "dead": False}

    while time.time() - t0 < 18.0:
        # send scheduled command
        if cmd_idx < len(cmds) and time.time() >= next_send:
            ch, wait = cmds[cmd_idx]
            ser.write(ch.encode())
            ser.flush()
            sent.append(ch)
            print(f"[{now()}] SEND '{ch}'")
            last_cmd = ch
            next_send = time.time() + wait
            cmd_idx += 1

        line = ser.readline().decode(errors="replace").rstrip("\r\n")
        if line:
            print(f"[{now()}] {line}")
            if "music player screen up" in line:
                saw["screen_up"] = True
            if "[CMD]" in line:
                saw["cmd"].append(line)
            if "decode" in line.lower() or "read" in line.lower() or "frame" in line.lower():
                saw["decode"] = True
            if "HardFault" in line or "Error_Handler" in line:
                saw["dead"] = True

    # Final liveness probe: send a harmless vol command and see if it echoes
    ser.write(b'+')
    ser.flush()
    time.sleep(1.0)
    alive_after = False
    while ser.in_waiting:
        line = ser.readline().decode(errors="replace").rstrip("\r\n")
        if line:
            print(f"[{now()}] {line}")
            if "[CMD] vol+" in line:
                alive_after = True

    ser.close()

    print("\n========== RESULT ==========")
    print(f"  music player screen up : {saw['screen_up']}")
    print(f"  commands echoed        : {len(saw['cmd'])} -> {saw['cmd']}")
    print(f"  decode/stream activity : {saw['decode']}")
    print(f"  hardfault seen         : {saw['dead']}")
    print(f"  responsive after cmds  : {alive_after}")
    cmds_ok = len(saw['cmd']) >= 3 and not saw['dead'] and alive_after
    print(f"  VERDICT                : {'PASS - board alive through p/n/v' if cmds_ok else 'FAIL'}")

if __name__ == "__main__":
    main()
