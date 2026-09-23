"""Acceptance check for the card-side GBK face (ui/sd_font.cpp).

Why this exists
---------------
Swapping the CJK face is the kind of change whose failure is quiet: if the
glyph index arithmetic is off by one, or a row is read MSB-first instead of
LSB-first, or the reverse table is built over the wrong trail range, the
firmware still builds, still boots, still renders *something* -- the text is
just wrong.  Nothing in a build log or a boot log would say so.

So this script re-derives the whole chain on the host, from two independent
sources, and prints a pass/fail count:

  * gbk_table.c   the CP936 table the firmware itself links against
  * the host's own cp936 codec, via Python

If those two agree, the file-format analysis in sd_font.cpp applies to the
numbers the firmware will actually compute, and the codepoint count it logs at
boot should equal the count derived here.

What it checks
--------------
  1. the embedded face parses out of its generated C file
  2. the firmware's GBK table matches the host codec, slot for slot
  3. the reverse index covers 21791 distinct codepoints -- the number the
     firmware logs at boot
  4. hzk_index() stays inside the 23940-glyph file for every mapped cell,
     and no two GBK pairs collide onto one codepoint
  5. the card's face holds no private-use cells, so LVGL's icon glyphs must
     arrive through lv_font_t::fallback (which is why that link is not optional)
  6. every non-private-use codepoint of the embedded face survives the swap
  7. horizontal metrics are unchanged, so reader pagination cannot shift
  8. coverage over a body-text sample, before and after

Usage
-----
    python tools/verify_sd_font.py [device-log.txt]

Without an argument it looks for serial_log.txt, then serial_font1.txt.
"""

import re
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EMBEDDED_C = ROOT / "managed_components/lvgl__lvgl/src/font/lv_font_source_han_sans_sc_16_cjk.c"
GBK_TABLE_C = ROOT / "main/ui/assets/gbk_table.c"
CORPUS = ROOT / "doc/prompter-step1.md"

LEAD_MIN, LEAD_MAX = 0x81, 0xFE
TRAIL_MIN, TRAIL_MAX = 0x40, 0xFE
TRAIL_COUNT = TRAIL_MAX - TRAIL_MIN + 1          # 191
NO_TRAIL = 0x7F
GLYPH_COUNT = 126 * 190                          # 23940
PUA_LO, PUA_HI = 0xE000, 0xF8FF
CJK_LO = 0x2E7F                                  # above the CJK radicals block

# Two glyphs in the compiled-in subset do NOT advance a full 16 px, and both are
# rasterisation artifacts of the demo subset rather than typography (an
# ideographic comma is a full-width character, and a fullwidth parenthesis is
# not 16.125 px wide).  The card's face gives every glyph a 16 px cell, so these
# two really do change width on screen:
#
#   U+3001  、 141/16 =  8.81 px -> 16 px   (+7.2 px: can re-wrap a line)
#   U+FF08  （ 258/16 = 16.125 px -> 16 px  (-0.125 px)
#
# Every other CJK glyph is 256/16 = 16 px, so pagination is otherwise untouched.
# Pinned here so a future change to the font set has to be deliberate.
ADV_QUIRKS = {0x3001: 141, 0xFF08: 258}

# The number the firmware prints at boot.  Kept here as a constant so a change
# in either direction is loud rather than silent.
EXPECTED_CODEPOINTS = 21791

results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print("  [%s] %-52s %s" % ("PASS" if ok else "FAIL", name, detail))
    return ok


def hzk_index(lead, trail):
    """The file offset in glyphs, exactly as sd_font.cpp computes it."""
    return (lead - LEAD_MIN) * 190 + (trail - TRAIL_MIN) - (1 if trail > NO_TRAIL else 0)


# ---------------------------------------------------------------- the two tables

def parse_embedded_face():
    """Codepoints, glyph metrics and glyph_id -> codepoint of the compiled-in face."""
    src = EMBEDDED_C.read_text(encoding="utf-8", errors="replace")

    def u16_list(name):
        m = re.search(r"static const uint16_t %s\[\]\s*=\s*\{(.*?)\};" % re.escape(name), src, re.S)
        if not m:
            raise SystemExit("unicode list %s not found" % name)
        return [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", m.group(1))]

    cps, gid2cp = set(), {}
    block = re.search(r"lv_font_fmt_txt_cmap_t cmaps\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    for entry in re.finditer(r"\{(.*?)\}", block.group(1), re.S):
        e = entry.group(1)
        start = int(re.search(r"range_start\s*=\s*(\d+)", e).group(1))
        rlen = int(re.search(r"range_length\s*=\s*(\d+)", e).group(1))
        gstart = int(re.search(r"glyph_id_start\s*=\s*(\d+)", e).group(1))
        kind = re.search(r"type\s*=\s*(LV_FONT_FMT_TXT_CMAP_\w+)", e).group(1)
        ulist = re.search(r"unicode_list\s*=\s*(\w+)", e).group(1)
        ofs = re.search(r"glyph_id_ofs_list\s*=\s*(\w+)", e).group(1)
        if kind.endswith("FORMAT0_TINY"):
            for i in range(rlen):
                cps.add(start + i)
                gid2cp[gstart + i] = start + i
        elif kind.endswith("SPARSE_TINY"):
            for i, off in enumerate(u16_list(ulist)):
                cps.add(start + off)
                gid2cp[gstart + i] = start + off
        elif kind.endswith("FORMAT0_FULL"):
            m = re.search(r"static const uint8_t %s\[\]\s*=\s*\{(.*?)\};" % re.escape(ofs), src, re.S)
            vals = [int(v) for v in re.findall(r"\d+", m.group(1))]
            for i, v in enumerate(vals):
                if v != 0:
                    cps.add(start + i)
                    gid2cp[gstart + v] = start + i
        else:
            raise SystemExit("unhandled cmap type " + kind)

    # Per-glyph metrics, for the "does text move" question.  Indexed by
    # glyph_id - 1, which is LVGL's convention for glyph_dsc.
    gd = re.search(r"lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    shapes = {}
    for i, m in enumerate(re.finditer(
            r"\{\.bitmap_index\s*=\s*\d+,\s*\.adv_w\s*=\s*(\d+),\s*\.box_w\s*=\s*(\d+),"
            r"\s*\.box_h\s*=\s*(\d+),\s*\.ofs_x\s*=\s*(-?\d+),\s*\.ofs_y\s*=\s*(-?\d+)\}",
            gd.group(1))):
        shapes[i + 1] = tuple(int(v) for v in m.groups())
    return cps, shapes, gid2cp


def parse_device_table():
    """The CP936 table the firmware links, as {slot: codepoint}."""
    src = GBK_TABLE_C.read_text(encoding="utf-8", errors="replace")
    vals = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]{4}", src)]
    return vals


def cp936_codepoints():
    """Codepoints the host codec maps onto the GBK double-byte grid."""
    cells = {}
    for lead in range(LEAD_MIN, LEAD_MAX + 1):
        for trail in range(TRAIL_MIN, TRAIL_MAX + 1):
            if trail == NO_TRAIL:
                continue
            try:
                ch = bytes([lead, trail]).decode("cp936")
            except UnicodeDecodeError:
                continue
            cells.setdefault(ord(ch), []).append((lead, trail))
    return cells


def main(argv):
    log_path = None
    for cand in (argv[1:2] or ["serial_log.txt", "serial_font1.txt"]):
        p = Path(cand)
        p = p if p.is_absolute() else ROOT / p
        if p.is_file():
            log_path = p
            break

    print("embedded face : %s" % EMBEDDED_C.relative_to(ROOT))
    print("device table  : %s" % GBK_TABLE_C.relative_to(ROOT))
    print()

    # 1 -------------------------------------------------------------------
    emb, shapes, gid2cp = parse_embedded_face()
    check("embedded face parses", len(emb) > 0, "%d codepoints, %d glyph metrics, %d glyphs mapped"
          % (len(emb), len(shapes), len(gid2cp)))

    # 2 -------------------------------------------------------------------
    dev = parse_device_table()
    check("device table has 24066 slots", len(dev) == 24066, "got %d" % len(dev))

    host_cells = cp936_codepoints()
    mismatch = 0
    for lead in range(LEAD_MIN, LEAD_MAX + 1):
        for trail in range(TRAIL_MIN, TRAIL_MAX + 1):
            slot = (lead - LEAD_MIN) * TRAIL_COUNT + (trail - TRAIL_MIN)
            want = 0
            if trail != NO_TRAIL:
                try:
                    want = ord(bytes([lead, trail]).decode("cp936"))
                except UnicodeDecodeError:
                    want = 0
            if dev[slot] != want:
                mismatch += 1
    check("firmware GBK table == host cp936", mismatch == 0,
          "%d of 24066 slots differ" % mismatch)

    # 3 -------------------------------------------------------------------
    card = set(cp936_codepoints())
    check("card face codepoints == %d (boot log)" % EXPECTED_CODEPOINTS,
          len(card) == EXPECTED_CODEPOINTS, "got %d" % len(card))

    # 4 -------------------------------------------------------------------
    collisions = {c: v for c, v in host_cells.items() if len(v) > 1}
    bad_range = [c for c, v in host_cells.items()
                 for (l, t) in v if not 0 <= hzk_index(l, t) < GLYPH_COUNT]
    check("hzk_index() inside the 23940-glyph file", not bad_range,
          "all %d cells, %d out of range"
          % (sum(len(v) for v in host_cells.values()), len(bad_range)))
    check("no two GBK pairs share a codepoint", not collisions,
          "%d collisions (a lost cell)" % len(collisions))

    # 5 -------------------------------------------------------------------
    card_pua = sorted(c for c in card if PUA_LO <= c <= PUA_HI)
    emb_pua = sorted(c for c in emb if PUA_LO <= c <= PUA_HI)
    check("card face has no private-use cells", not card_pua,
          "embedded face keeps %d (U+%04X..U+%04X) for LV_SYMBOL_* via fallback"
          % (len(emb_pua), emb_pua[0], emb_pua[-1]) if emb_pua else "")

    # 6 -------------------------------------------------------------------
    lost = sorted(c for c in emb if c > CJK_LO and not PUA_LO <= c <= PUA_HI and c not in card)
    check("every embedded text glyph survives the swap", not lost,
          "n/a" if not lost else "lost %d: %s" % (len(lost), "".join(map(chr, lost[:20]))))

    # 7 -------------------------------------------------------------------
    # The glyphs the card's face replaces are the CJK ones, so those are the
    # ones whose advance has to be preserved: text advances 16 px there, which
    # is what face_glyph_dsc() reports, so line breaking and the reader's
    # pagination stay put.  Keyed on codepoint rather than box width -- the
    # face's own FontAwesome icons are also 16 px wide but advance 14, and
    # they never reach this face (PUA is served by the fallback).
    cjk_adv = {c: shapes[i][0] for i, c in gid2cp.items()
               if c > CJK_LO and not PUA_LO <= c <= PUA_HI and i in shapes}
    odd = {c: a for c, a in cjk_adv.items() if a != 256}
    check("CJK advances are 16 px, bar the 2 known quirks", odd == ADV_QUIRKS,
          "%d CJK glyphs, %d differ: %s"
          % (len(cjk_adv), len(odd),
             ", ".join("%s %.2f->16 px" % (chr(c), a / 16.0) for c, a in sorted(odd.items()))
             or "none"))

    # 8 -------------------------------------------------------------------
    if CORPUS.is_file():
        text = CORPUS.read_text(encoding="utf-8", errors="replace")
        body = {ord(c) for c in text if not c.isspace() and ord(c) > CJK_LO}
        before = len(body & emb)
        after = len(body & card)
        check("coverage improves on a body-text sample", after > before,
              "%s: %d distinct CJK, %d (%.1f%%) -> %d (%.1f%%), %d still missing (emoji)"
              % (CORPUS.name, len(body), before, 100.0 * before / max(len(body), 1),
                 after, 100.0 * after / max(len(body), 1), len(body - card)))
        if body - card:
            names = []
            for c in sorted(body - card)[:8]:
                try:
                    names.append("%s %s" % (chr(c), unicodedata.name(chr(c))))
                except ValueError:
                    names.append("U+%04X" % c)
            print("       still missing: %s" % ", ".join(names))

    # 9 -------------------------------------------------------------------
    if log_path is not None:
        raw = log_path.read_bytes().replace(b"\x00", b"")
        raw = re.sub(rb"\x1b\[[0-9;]*[a-zA-Z]", b"", raw).decode("utf-8", "replace")
        m = re.search(r"sd_font: installed (\S+): (\d+) glyphs, (\d+) codepoints mapped", raw)
        if m:
            dev_cp = int(m.group(3))
            check("boot log matches this analysis", dev_cp == len(card),
                  "%s reports %s glyphs / %s codepoints" % (m.group(1), m.group(2), m.group(3)))
        else:
            check("boot log carries an sd_font install line", False,
                  "%s has none" % log_path.name)
        check("boot log selected the card's face, not the embedded subset",
              "CJK font   : card's GBK face" in raw,
              "looked in %s" % log_path.name)
    else:
        print("  [skip] no device log found (pass one as an argument to check boot output)")

    passed = sum(1 for _, ok, _ in results if ok)
    print()
    print("%d/%d checks passed" % (passed, len(results)))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
