#!/usr/bin/env python3
"""Generate the GBK (code page 936) -> Unicode table used by text_service.c.

WHY A GENERATED TABLE AT ALL
----------------------------
A Chinese .txt file on a Windows machine is almost always encoded in GBK, not
UTF-8.  The device has no iconv, no font engine and no locale, so the only way
to turn those bytes into something LVGL can draw is a codepage table compiled
into the image.  CP936 maps every two-byte sequence in 0x81..0xFE / 0x40..0xFE
to one BMP code point, so the table is dense and small: a flat uint16 array of
126 x 191 entries, ~48 KB.  A sparse table would save nothing worth the lookup
cost, and a hand-written one would be wrong within the first hundred rows.

The table is generated from the host Python's cp936 codec rather than typed out,
so it is reproducible: run this script, diff the output, done.

LAYOUT
------
    gbk_unicode_table[(lead - 0x81) * GBK_TRAIL_COUNT + (trail - 0x40)]

Slots that CP936 does not define (and trail byte 0x7F, which is never valid)
hold 0x0000, which the C side reads as "unmappable".  0x80 is not part of the
two-byte space either; it is handled as a one-byte special case in C because
CP936 maps it to U+20AC (euro sign).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

LEAD_MIN, LEAD_MAX = 0x81, 0xFE
TRAIL_MIN, TRAIL_MAX = 0x40, 0xFE
TRAIL_COUNT = TRAIL_MAX - TRAIL_MIN + 1  # 191

# Bytes CP936 defines as single-byte characters.  Everything else below 0x80 is
# plain ASCII; a lone byte >= 0x80 that is not a valid lead is dropped by the C
# side with a counter, which is how a truncated file becomes visible instead of
# silently mangled.
SINGLE_BYTE = {0x80: 0x20AC}  # euro sign


def build_table() -> list[int]:
    table = [0x0000] * ((LEAD_MAX - LEAD_MIN + 1) * TRAIL_COUNT)
    unmapped = 0

    for lead in range(LEAD_MIN, LEAD_MAX + 1):
        base = (lead - LEAD_MIN) * TRAIL_COUNT
        for trail in range(TRAIL_MIN, TRAIL_MAX + 1):
            if trail == 0x7F:  # never a valid trail byte
                unmapped += 1
                continue
            raw = bytes((lead, trail))
            try:
                text = raw.decode("cp936")
            except UnicodeDecodeError:
                unmapped += 1
                continue
            if len(text) != 1:
                # A CP936 lead/trail pair that expands to more than one code
                # point (or to a surrogate) cannot be stored in one uint16.
                unmapped += 1
                continue
            code = ord(text)
            if code > 0xFFFF:
                unmapped += 1
                continue
            table[base + trail - TRAIL_MIN] = code

    mapped = len(table) - unmapped
    print(f"cp936: {mapped} mapped, {unmapped} unmapped, {len(table)} slots")
    return table


def sanity_check(table: list[int]) -> None:
    """Spot-check a handful of sequences whose values are worth knowing by heart.

    A table that is off by one row would still look plausible, so these are
    pinned: they are the characters that appear in every Chinese test file.
    """
    def lookup(lead: int, trail: int) -> int:
        return table[(lead - LEAD_MIN) * TRAIL_COUNT + (trail - TRAIL_MIN)]

    # CP936 wire values for 你 好 世 界 中 文 测 试
    expected = {
        (0xC4, 0xE3): 0x4F60,  # 你
        (0xBA, 0xC3): 0x597D,  # 好
        (0xCA, 0xC0): 0x4E16,  # 世
        (0xBD, 0xE7): 0x754C,  # 界
        (0xD6, 0xD0): 0x4E2D,  # 中
        (0xCE, 0xC4): 0x6587,  # 文
        (0xB2, 0xE2): 0x6D4B,  # 测
        (0xCA, 0xD4): 0x8BD5,  # 试
    }
    bad = []
    for (lead, trail), want in expected.items():
        got = lookup(lead, trail)
        if got != want:
            bad.append(f"0x{lead:02X}{trail:02X} -> 0x{got:04X}, expected 0x{want:04X}")
        # And the round trip the device performs:
        if got:
            again = got.to_bytes(2, "little").decode("utf-16-le")
            if again.encode("cp936") != bytes((lead, trail)):
                bad.append(f"0x{lead:02X}{trail:02X} round trip mismatch")
    if bad:
        for line in bad:
            print(f"  FAIL {line}", file=sys.stderr)
        raise SystemExit("generated table failed its own sanity check")
    print("sanity: 8/8 pinned characters round trip through cp936")


def emit(table: list[int], out_c: Path, out_h: Path) -> None:
    rows = []
    for lead in range(LEAD_MIN, LEAD_MAX + 1):
        base = (lead - LEAD_MIN) * TRAIL_COUNT
        chunk = table[base:base + TRAIL_COUNT]
        text = "".join(f"0x{v:04X}," for v in chunk)
        # Wrap so the file stays readable; 12 values per line is what fits a
        # 100-column editor without the reader losing their place.
        cells = text.split(",")[:-1]
        lines = ["    " + "".join(f"{c}," for c in cells[i:i + 12]) for i in range(0, len(cells), 12)]
        rows.append(f"    /* 0x{lead:02X} */\n" + "\n".join(lines))

    header = f"""/**
 * @file gbk_table.h
 * @brief CP936 (GBK) -> Unicode BMP lookup table. GENERATED - DO NOT EDIT.
 *
 * Regenerate with:  python tools/gen_gbk_table.py
 * Source of truth:   the host's cp936 codec (i.e. Microsoft code page 936).
 *
 * Layout:  gbk_unicode_table[(lead - 0x{LEAD_MIN:02X}) * GBK_TRAIL_COUNT +
 *                            (trail - 0x{TRAIL_MIN:02X})]
 * 0x0000 marks a slot CP936 does not define.
 */
#pragma once

#include <stdint.h>

#define GBK_LEAD_MIN    (0x{LEAD_MIN:02X}u)
#define GBK_LEAD_MAX    (0x{LEAD_MAX:02X}u)
#define GBK_TRAIL_MIN   (0x{TRAIL_MIN:02X}u)
#define GBK_TRAIL_MAX   (0x{TRAIL_MAX:02X}u)
#define GBK_TRAIL_COUNT ({TRAIL_COUNT}u)

/** @brief CP936 maps this byte to the euro sign rather than to a lead byte. */
#define GBK_SINGLE_EURO (0x80u)

/** @brief {(LEAD_MAX - LEAD_MIN + 1) * TRAIL_COUNT} entries, little used slots hold 0. */
extern const uint16_t gbk_unicode_table[{len(table)}];
"""

    body = f"""/**
 * @file gbk_table.c
 * @brief CP936 (GBK) -> Unicode BMP lookup table. GENERATED - DO NOT EDIT.
 *
 * See tools/gen_gbk_table.py for why this is generated rather than hand-written
 * and how to reproduce it.
 */

#include "gbk_table.h"

const uint16_t gbk_unicode_table[{len(table)}] = {{
""" + "\n".join(rows) + "\n};\n"

    out_h.write_text(header, encoding="utf-8", newline="\n")
    out_c.write_text(body, encoding="utf-8", newline="\n")
    print(f"wrote {out_c} ({out_c.stat().st_size} bytes)")
    print(f"wrote {out_h} ({out_h.stat().st_size} bytes)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out-dir", default=None,
                    help="directory for gbk_table.{c,h} (default: main/ui/assets)")
    args = ap.parse_args()

    here = Path(__file__).resolve().parent
    out_dir = Path(args.out_dir) if args.out_dir else here.parent / "main" / "ui" / "assets"
    out_dir.mkdir(parents=True, exist_ok=True)

    table = build_table()
    sanity_check(table)
    emit(table, out_dir / "gbk_table.c", out_dir / "gbk_table.h")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
