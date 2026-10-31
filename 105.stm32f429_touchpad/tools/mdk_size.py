#!/usr/bin/env python3
"""Print a FLASH/RAM/SDRAM usage table for a uVision build directory.

Reads the armlink map file, so the numbers are available even when UV4 skipped
the link step (nothing changed), which is when "Program Size:" is not printed.

    python tools/mdk_size.py MDK-ARM/Objects/Debug
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REGIONS = {
    "ER_IROM1": ("FLASH", 1024 * 1024, "1 MB"),
    "RW_IRAM1": ("RAM", 192 * 1024, "192 KB"),
    "RW_SDRAM": ("SDRAM", 32 * 1024 * 1024, "32 MB"),
}

REGION_RE = re.compile(
    r"Execution Region (\w+) \(Exec base: 0x[0-9a-fA-F]+,"
    r".*?Size: (0x[0-9a-fA-F]+)"
)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    build = Path(sys.argv[1])
    maps = sorted(build.glob("*.map"))
    if not maps:
        print(f"  no map file in {build}")
        return 0

    text = maps[0].read_text(encoding="utf-8", errors="replace")
    sizes = {name: int(value, 16) for name, value in REGION_RE.findall(text)}

    # ER_IROM1 holds the code; the initialised-data load copy follows it, and
    # that is exactly the .bin size.  Fall back to the region size if the .bin
    # has not been produced.
    bins = sorted(build.glob("*.bin"))
    flash = bins[0].stat().st_size if bins else sizes.get("ER_IROM1", 0)

    for key, (label, total, human) in REGIONS.items():
        if key == "ER_IROM1":
            used = flash
        elif key in sizes:
            used = sizes[key]
        else:
            continue
        print(f"{label + ':':>16}{used:>10} B  {human:>9}{used / total * 100:9.2f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
