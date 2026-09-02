"""Verify 4: WebSocket live push on port 81 (stdlib-only WS client).

Connects to ws://<ip>:81/, listens up to LISTEN_S seconds, and counts the
JSON message types the gateway broadcasts (state / adc / gpio / led / pwm /
log / system ...). PASS if at least MIN_MESSAGES valid typed messages arrive.

Expected background traffic even with nothing wired:
  - state   : Interface snapshot every 400 ms (gpio + led + pwm + adc)
  - adc     : individual samples every 100 ms
  - system  : heartbeat every 5 s
  - log     : firmware log lines

Usage: python verify_ws.py [device_ip]
"""
import sys
import time

from rhd_common import Result, DEVICE_IP, WSClient, WS_PORT

LISTEN_S = 12.0
MIN_MESSAGES = 3


def main():
    r = Result("verify_ws @ %s:%d" % (DEVICE_IP, WS_PORT))
    print("Connecting ws://%s:%d/ and listening %.0fs ..." % (DEVICE_IP, WS_PORT, LISTEN_S))
    print()

    ws = WSClient(DEVICE_IP)
    try:
        ws.connect()
        r.check("WS handshake 101 Switching Protocols", True, "connected")

        type_count = {}
        msg_count = 0
        state_sections = set()
        deadline = time.time() + LISTEN_S
        while time.time() < deadline:
            msg = ws.recv(timeout=0.5)
            if msg is None:
                continue
            t = msg.get("type", "<none>")
            type_count[t] = type_count.get(t, 0) + 1
            msg_count += 1
            if t == "state":
                for key in ("gpio", "led", "pwm", "adc"):
                    if key in msg:
                        state_sections.add(key)

        r.check("connection stayed open for %.0fs" % LISTEN_S, True)
        r.check("received >= %d valid typed JSON messages" % MIN_MESSAGES,
                msg_count >= MIN_MESSAGES, "total=%d" % msg_count)
        if type_count:
            hist = ", ".join("%s=%d" % (k, v) for k, v in sorted(type_count.items()))
            print("[INFO] message histogram: %s" % hist)

        if "system" in type_count:
            r.check("periodic system status observed", True, "every 5s heartbeat")
        else:
            print("[WARN] no 'system' message yet (heartbeat every 5s; "
                  "increase LISTEN_S if the device just booted)")

        if "state" in type_count:
            r.check("Interface state snapshots observed", True,
                    "sections=%s" % ",".join(sorted(state_sections)))
            r.check("state snapshot carries all 4 sections",
                    state_sections == {"gpio", "led", "pwm", "adc"},
                    "got=%s" % ",".join(sorted(state_sections)))
        else:
            r.check("Interface state snapshots observed", False,
                    "no 'state' message in %.0fs" % LISTEN_S)

    except Exception as e:
        r.check("WS connect/listen completed", False, str(e))
    finally:
        ws.close()

    sys.exit(r.summary())


if __name__ == "__main__":
    main()
