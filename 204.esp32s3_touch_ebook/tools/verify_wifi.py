"""Acceptance check for the WiFi station service (services/net_service.cpp).

Why this exists
---------------
Every fault this file guards against was found on hardware, not in a build log,
and every one of them is invisible to the compiler:

  * a scan issued while associating raised a plain disconnect, which the
    service counted as a failed attempt -- the counter then claimed three
    retries when only two had been issued, and the log showed "attempt 2/3"
    with no "attempt 1/3";
  * esp_timer_start_once() on an already-armed timer answers
    ESP_ERR_INVALID_STATE and does *not* restart it, so a second failure inside
    one retry window spent a slot without buying an attempt;
  * a bare esp_wifi_connect() with no station config (which is what the boot
    path used to do, since the driver's config does not survive a reset under
    WIFI_STORAGE_RAM) returns ESP_ERR_WIFI_SSID, raises no event, and leaves
    the published state on "connecting" for ever.

So this script reads captured device logs and checks the state machine that the
logs describe, including the two orderings that are only visible in timestamps.
The retry budget and delay are parsed out of the source first, so the script
fails rather than silently agreeing with itself if the constants move.

Usage
-----
    python tools/verify_wifi.py [log.txt ...]

Defaults to serial_wifi6.txt, serial_wifi7.txt, serial_wifi9.txt -- the
failure-path run, the open-AP run and the plain boot run.  Logs that do not
contain a given phase are skipped with a note rather than failed.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "main/services/net_service.cpp"
DEFAULT_LOGS = ["serial_wifi6.txt", "serial_wifi7.txt", "serial_wifi9.txt"]

results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print("  [%s] %-56s %s" % ("PASS" if ok else "FAIL", name, detail))
    return ok


def clean(path):
    raw = path.read_bytes().replace(b"\x00", b"")
    raw = re.sub(rb"\x1b\[[0-9;]*[a-zA-Z]", b"", raw)
    return raw.decode("utf-8", "replace")


# --------------------------------------------------------------- the constants

def read_constants():
    """The retry budget and delay, read from the source of truth."""
    src = SOURCE.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"constexpr int kMaxRetries\s*=\s*(\d+)", src)
    d = re.search(r"constexpr int kRetryDelayMs\s*=\s*(\d+)", src)
    return (int(m.group(1)) if m else None), (int(d.group(1)) if d else None)


# ------------------------------------------------------------ log line helpers

TS = r"\((\d+)\)"


def net_lines(text):
    """(timestamp_ms, severity, message) for every net: line."""
    out = []
    for line in text.splitlines():
        m = re.search(r"^[IWEDV]\s+\((\d+)\)\s+net:\s?(.*)$", line.strip())
        if m:
            out.append((int(m.group(1)), line.strip()[0], m.group(2).strip()))
    return out


def phases(lines):
    return {
        "attempts": [(t, m) for t, _, m in lines if m.startswith("attempt ")],
        "failures": [(t, m) for t, _, m in lines if m.startswith("disconnected: reason")],
        "ignored": [(t, m) for t, _, m in lines if m.startswith("link down (reason")],
        "erased": [(t, m) for t, _, m in lines if "credentials erased" in m],
        "connected": [(t, m) for t, _, m in lines if m.startswith("connected, ip")],
    }


# ------------------------------------------------------------------ the checks

def check_common(text, lines, name, max_retries):
    n = len(lines)
    check("%s: is a net: log" % name, n > 0, "%d net: lines" % n)
    if n == 0:
        return

    check("%s: driver keeps no credential copy" % name,
          "wifi:config NVS flash: disabled" in text,
          "CONFIG_ESP_WIFI_NVS_ENABLED=n took effect")

    m = re.search(r"station up; internal heap (\d+) -> (\d+) B \(cost (\d+) B\)", text)
    if m:
        before, after, cost = (int(m.group(i)) for i in (1, 2, 3))
        check("%s: internal-SRAM cost is stated and matches" % name,
              before - after == cost and 0 < cost < 250000,
              "%d -> %d B, cost %d B" % (before, after, cost))
    else:
        check("%s: logs the internal-SRAM cost of bringing the station up" % name, False)

    # A local leave is never a failure.  This is the first bug: the scan-pause's
    # own disconnect was counted, which is what made "attempt 1/3" disappear.
    counted_leave = [t for t, m in phases(lines)["failures"]
                     if re.search(r"reason (8|36) ", m)]
    check("%s: no local leave counted as a failure" % name, not counted_leave,
          "%d such line(s)" % len(counted_leave))

    silent = [m for _, m in phases(lines)["ignored"] if not m.endswith("nothing to do")]
    check("%s: every local leave is logged as ignored" % name, not silent,
          "%d of %d" % (len(phases(lines)["ignored"]) - len(silent),
                        len(phases(lines)["ignored"])))

    bad = [k for k in ("ERROR***", "Guru Meditation", "Backtrace:", "assert failed")
           if k in text]
    check("%s: no crash or assert in the log" % name, not bad, ",".join(bad))


def check_retry_sequence(lines, name, max_retries, delay_ms):
    p = phases(lines)
    attempts = p["attempts"]
    if not attempts:
        print("  [skip] %s: no retry sequence in this log" % name)
        return

    nums = [int(re.match(r"attempt (\d+)/(\d+)", m).group(1)) for _, m in attempts]
    dens = [int(re.match(r"attempt (\d+)/(\d+)", m).group(2)) for _, m in attempts]

    # The bug this catches: slot 1 consumed by something that was not an attempt.
    check("%s: attempt numbers are 1..%d with no gap" % (name, max_retries),
          nums == list(range(1, len(nums) + 1)) and len(nums) == max_retries,
          "saw %s" % nums)
    check("%s: the denominator matches kMaxRetries=%d" % (name, max_retries),
          all(d == max_retries for d in dens), "saw %s" % sorted(set(dens)))

    # Each attempt must be preceded by a failure and come one delay later: a
    # timer that failed to arm would show the attempt arriving immediately.
    gaps = []
    for i, (t, _) in enumerate(attempts):
        prev = [ft for ft, _ in p["failures"] if ft < t]
        if prev:
            gaps.append(t - max(prev))
    check("%s: every attempt waits out the retry delay" % name,
          bool(gaps) and all(g >= delay_ms for g in gaps),
          "gaps %s ms vs delay %d ms" % (gaps, delay_ms))

    gave_up = [m for _, _, m in lines if m.startswith("giving up")]
    check("%s: gives up once the budget is spent" % name,
          any("after %d retries" % max_retries in m for m in gave_up),
          "; ".join(gave_up) or "no give-up line")


def check_forget(lines, name):
    p = phases(lines)
    if not p["erased"]:
        print("  [skip] %s: forget() was not exercised in this log" % name)
        return
    t_erase = p["erased"][0][0]
    late = [t for t, _ in p["attempts"] if t > t_erase]
    check("%s: no retry survives forget()" % name, not late, "late attempts %s" % late)
    check("%s: an idle station raises no event after forget()" % name,
          not [t for t, _ in p["ignored"] if t > t_erase],
          "nothing published after erasing the credentials")


def check_connected(lines, name):
    p = phases(lines)
    if not p["connected"]:
        print("  [skip] %s: no successful association in this log" % name)
        return
    t, msg = p["connected"][0]
    m = re.search(r"connected, ip (\d+\.\d+\.\d+\.\d+), rssi (-?\d+) dBm", msg)
    check("%s: publishes a real lease and signal" % name,
          bool(m) and m.group(1) != "0.0.0.0" and int(m.group(2)) < 0,
          msg if not m else "%s at %d ms, rssi %s dBm" % (m.group(1), t, m.group(2)))

    auth = [t for t, _, m in lines if m.startswith("associated with")]
    check("%s: DHCP completes after the association" % name,
          bool(auth) and auth[0] < t, "associated at %s, leased at %d" % (auth[:1], t))


def main(argv):
    logs = []
    for a in argv[1:]:
        p = Path(a)
        if not p.is_absolute():
            p = ROOT / p
        logs.append(p)
    if not logs:
        logs = [ROOT / n for n in DEFAULT_LOGS]

    max_retries, delay_ms = read_constants()
    print("source        : %s" % SOURCE.relative_to(ROOT))
    print("retry budget  : %s retries, %s ms apart" % (max_retries, delay_ms))
    if max_retries is None or delay_ms is None:
        print("\nCannot read kMaxRetries/kRetryDelayMs from the source.\n")
        return 1

    read_any = False
    for path in logs:
        if not path.exists():
            print("\n%s: not found, skipped" % path.name)
            continue
        read_any = True
        print("\n%s" % path.name)
        text = clean(path)
        lines = net_lines(text)
        check_common(text, lines, path.name, max_retries)
        check_retry_sequence(lines, path.name, max_retries, delay_ms)
        check_forget(lines, path.name)
        check_connected(lines, path.name)

    if not read_any:
        # 0/0 would print as a pass, and a script that goes green because it read
        # nothing is worse than no script.
        print("\nNo log could be read. The captures are not in the repository;")
        print("take one and pass it in:  python tools/verify_wifi.py serial_wifiN.txt")
        return 1

    passed = sum(1 for _, ok, _ in results if ok)
    print()
    print("%d/%d checks passed" % (passed, len(results)))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
