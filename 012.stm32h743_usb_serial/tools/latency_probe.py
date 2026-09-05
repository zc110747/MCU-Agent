#!/usr/bin/env python3
"""DEPRECATED -- DO NOT USE.

This probe compared UART loopback latency against the firmware's *software*
loopback (old design: AT+LOOP=1, which bypassed UART/DMA entirely).

The bridge firmware was redesigned to be a *transparent* CDC<->UART4 forwarder
with NO in-band AT command channel, so AT+LOOP no longer exists. This script
imports stress_test.at_session / at_exit, which were removed, and cannot run.

If you still need to separate host-side vs firmware-side latency contributions,
the technique lives on in tools/stress_test.py (subcommand `paced`, which
measures round-trip latency with n = ser.in_waiting; ser.read(n if n else 1))
and is documented in README.md (section 8.4, "latency measurement caveat").
The PC-side false-latency root cause (ser.read(4096) waiting for a full buffer)
is a cross-project note in ~/.workbuddy/MEMORY.md, section 五.
"""
import sys


def main() -> int:
    sys.stderr.write(
        "latency_probe.py is DEPRECATED and non-functional after the "
        "transparent bridge redesign (no AT+LOOP software loopback). Use "
        "stress_test.py paced for latency measurement.\n"
    )
    return 2


if __name__ == "__main__":
    sys.exit(main())
