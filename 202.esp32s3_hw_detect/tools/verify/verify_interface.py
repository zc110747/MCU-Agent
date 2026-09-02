"""Verify 5: Interface live state over WebSocket (GPIO / WS2812 / PWM / ADC).

Drives the four Interface widgets through the WebSocket command channel and
asserts that the device's periodic `state` snapshot (pushed every 400 ms)
reflects the change -- i.e. the web UI shows real hardware state, not an echo
of what was typed.

Checks:
  1. `state` snapshot arrives and carries gpio / led / pwm / adc sections
  2. gpio_set value=1 -> state.gpio[pin].state==1 and dir==1 (promoted to OUT)
     gpio_set value=0 -> state.gpio[pin].state==0
  3. ws2812_set cycle -> state.led.mode_str=="cycle" and the live on/color
     fields actually move over time; ws2812_set off -> on==0
  4. pwm_set pin/period/duty -> state.pwm mirrors the applied values
     pwm_set active:false -> state.pwm.active==0
  5. state.adc has 4 channels (voltage numeric)

Usage: python verify_interface.py [device_ip]
"""
import sys
import time

from rhd_common import Result, DEVICE_IP, WSClient

GPIO_PIN = 4          # one of GPIO_MONITOR_PINS (4,5,6,7)
PWM_PIN = 21          # free GPIO, not claimed at boot
PWM_PERIOD = 1000     # us
PWM_DUTY = 25         # %

STATE_TIMEOUT = 6.0
LIVE_WINDOW_S = 2.0


def gpio_field(state, pin):
    for g in state.get("gpio") or []:
        if g.get("pin") == pin:
            return g
    return None


def main():
    r = Result("verify_interface @ %s:81" % DEVICE_IP)
    print("Driving Interface widgets over WS and reading back 'state' snapshots")
    print()

    ws = WSClient(DEVICE_IP)
    try:
        ws.connect()
        r.check("WS handshake 101", True, "connected")

        # ---------------------------------------------------------- 1. snapshot
        state = ws.wait_type("state", STATE_TIMEOUT)
        if state is None:
            r.check("state snapshot received", False, "no 'state' message within %.0fs" % STATE_TIMEOUT)
            sys.exit(r.summary())
        r.check("state snapshot received", True)
        r.check("state carries gpio[]", isinstance(state.get("gpio"), list),
                "n=%d" % len(state.get("gpio") or []))
        r.check("state carries led{}", isinstance(state.get("led"), dict))
        r.check("state carries pwm{}", isinstance(state.get("pwm"), dict))
        r.check("state carries adc[]", isinstance(state.get("adc"), list),
                "n=%d" % len(state.get("adc") or []))

        # -------------------------------------------------------------- 2. GPIO
        ws.send({"cmd": "gpio_set", "gpio": GPIO_PIN, "value": 1})
        hi = ws.wait_for(lambda m: m.get("type") == "state"
                         and (gpio_field(m, GPIO_PIN) or {}).get("state") == 1,
                         STATE_TIMEOUT)
        g = gpio_field(hi, GPIO_PIN) if hi else None
        r.check("gpio_set HIGH -> state GPIO%d == HIGH" % GPIO_PIN, hi is not None,
                ("state=%s dir=%s" % (g.get("state"), g.get("dir"))) if g else "no update")
        r.check("GPIO%d promoted to output" % GPIO_PIN,
                g is not None and g.get("dir") == 1,
                ("dir=%s" % g.get("dir")) if g else "-")

        ws.send({"cmd": "gpio_set", "gpio": GPIO_PIN, "value": 0})
        lo = ws.wait_for(lambda m: m.get("type") == "state"
                         and (gpio_field(m, GPIO_PIN) or {}).get("state") == 0,
                         STATE_TIMEOUT)
        g = gpio_field(lo, GPIO_PIN) if lo else None
        r.check("gpio_set LOW -> state GPIO%d == LOW" % GPIO_PIN, lo is not None,
                ("state=%s" % g.get("state")) if g else "no update")

        # ------------------------------------------------------------ 3. WS2812
        prev_mode = (state.get("led") or {}).get("mode_str", "off")
        ws.send({"cmd": "ws2812_set", "mode": "cycle"})
        cyc = ws.wait_for(lambda m: m.get("type") == "state"
                          and (m.get("led") or {}).get("mode_str") == "cycle",
                          STATE_TIMEOUT)
        r.check("ws2812_set cycle -> state.led.mode_str == cycle", cyc is not None,
                (str((cyc or {}).get("led"))) if cyc else "no update")

        # Live output must move while cycling (that is the "actual state").
        seen = set()
        deadline = time.time() + LIVE_WINDOW_S
        while time.time() < deadline:
            m = ws.recv(timeout=0.4)
            if not m or m.get("type") != "state":
                continue
            led = m.get("led") or {}
            if led.get("mode_str") == "cycle":
                seen.add((int(led.get("on", 0)), int(led.get("r", 0)),
                          int(led.get("g", 0)), int(led.get("b", 0))))
        r.check("WS2812 live output changes over %.1fs" % LIVE_WINDOW_S, len(seen) >= 2,
                "distinct states=%d %s" % (len(seen), sorted(seen)[:4]))

        ws.send({"cmd": "ws2812_set", "mode": "off"})
        off = ws.wait_for(lambda m: m.get("type") == "state"
                          and (m.get("led") or {}).get("mode_str") == "off"
                          and int((m.get("led") or {}).get("on", 1)) == 0,
                          STATE_TIMEOUT)
        r.check("ws2812_set off -> state.led.on == 0", off is not None)

        # ---------------------------------------------------------------- 4. PWM
        ws.send({"cmd": "pwm_set", "pin": PWM_PIN, "period": PWM_PERIOD, "duty": PWM_DUTY})
        pw = ws.wait_for(lambda m: m.get("type") == "state"
                         and (m.get("pwm") or {}).get("active") == 1
                         and (m.get("pwm") or {}).get("pin") == PWM_PIN,
                         STATE_TIMEOUT)
        p = (pw or {}).get("pwm") or {}
        r.check("pwm_set -> state.pwm.active == 1", pw is not None, str(p))
        r.check("state.pwm mirrors applied parameters",
                p.get("period") == PWM_PERIOD and p.get("duty") == PWM_DUTY,
                "pin=%s period=%s duty=%s freq=%s" % (p.get("pin"), p.get("period"),
                                                      p.get("duty"), p.get("freq")))

        ws.send({"cmd": "pwm_set", "active": False})
        st = ws.wait_for(lambda m: m.get("type") == "state"
                         and (m.get("pwm") or {}).get("active") == 0,
                         STATE_TIMEOUT)
        r.check("pwm_set active:false -> state.pwm.active == 0", st is not None)

        # ---------------------------------------------------------------- 5. ADC
        adc = ws.wait_for(lambda m: m.get("type") == "state" and len(m.get("adc") or []) == 4,
                          STATE_TIMEOUT)
        if adc is None:
            r.check("state.adc has 4 channels", False, "no snapshot with 4 channels")
        else:
            channels = adc["adc"]
            numeric = all(isinstance(c.get("voltage"), (int, float)) for c in channels)
            r.check("state.adc has 4 channels with numeric voltage", numeric,
                    ", ".join("%s=%.3fV" % (c.get("ch"), c.get("voltage", 0)) for c in channels))
            if all(int(c.get("raw", 0)) == 0 for c in channels):
                print("[WARN] all ADC raw values are 0 - no signal / no ADS1115 wired")

        # ----------------------------------------------------------- restore LED
        ws.send({"cmd": "ws2812_set", "mode": prev_mode})
        print("[INFO] restored WS2812 mode to '%s'" % prev_mode)

    except Exception as e:
        r.check("Interface verification completed", False, str(e))
    finally:
        ws.close()

    sys.exit(r.summary())


if __name__ == "__main__":
    main()
