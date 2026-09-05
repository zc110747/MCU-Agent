#!/usr/bin/env python3
"""DEPRECATED -- DO NOT USE.

This probe was written for the OLD firmware design that exposed an in-band AT
command channel (AT+FLOW / AT+LOOP / AT+UART) plus DTR-driven gating.

The bridge firmware was redesigned to be a *transparent* CDC<->UART4 forwarder:
  - NO in-band AT commands (bulk data must never be intercepted).
  - Configuration is only via standard CDC control transfers
    (SET_LINE_CODING / SET_CONTROL_LINE_STATE).
  - Flow control is firmware-self-managed (CTSE always on + RTS driven by GPIO
    per receive-ring headroom). DTR is observed only; UART4 has no DTR pin.

The symbols this script imported (stress_test.at_session / at_exit, and the
AT+ commands) no longer exist, so it cannot run.

Replacements:
  - Hardware flow-control behaviour: tools/flow_hw_probe.py (run on the real
    wired link; under Windows usbser.sys does NOT forward RTS, only DTR -- see
    that file's header for the [2]/[3] gating caveat).
  - Transparency / standard-CDC acceptance: tools/stress_test.py (subcommands
    list / identify / stress / setbaud / formats / paced / all) and
    tools/transparency_probe.py.
"""
import sys


def main() -> int:
    sys.stderr.write(
        "dtr_probe.py is DEPRECATED and non-functional after the transparent "
        "bridge redesign (no in-band AT channel). Use flow_hw_probe.py for "
        "hardware flow-control verification, and stress_test.py / "
        "transparency_probe.py for acceptance.\n"
    )
    return 2


if __name__ == "__main__":
    sys.exit(main())
