#!/usr/bin/env python3
"""ttf2ctf - build a CTF index for a TrueType font.

    ttf2ctf.py input.ttf [-o out.ctf]
                         [--encoding unicode] [--font-index N]
                         [--verify] [--no-crc] [--verbose]
                         [--info] [--dump CHARS] [--coverage]

The CTF file is a *character index* into the original TTF: it maps a Unicode
code point to a glyph id plus that glyph's absolute byte range inside the TTF,
using three levels of direct addressing (plane -> page -> 256-bit bitmap).
It stores no bitmaps and no outline data, so the .ttf must stay next to it on
the SD card.

Only code points the font actually ships are indexed.  Anything else is a
cleared bit in a page bitmap, so the firmware returns NOT_FOUND without ever
touching the TTF - which is the whole point of the exercise.

Typical run for the HarmonyOS Sans family:

    ttf2ctf.py HarmonyOS_Sans_TC_Regular.ttf --verify --info --coverage
"""

from __future__ import annotations

import argparse
import os
import random
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ctf_format import (  # noqa: E402
    CTF_FLAG_HAS_COMPOSITE,
    CTF_FLAG_HAS_GPOS,
    CTF_FLAG_HAS_KERN,
    CTF_GLYPH_COMPOSITE,
    CTF_GLYPH_EMPTY,
    CTF_GLYPH_SIMPLE,
    CTF_GLYPH_VALID,
    CTF_HEADER_SIZE,
    CTF_L1_ENTRIES,
    CTF_L1_SIZE,
    CTF_MODE_NAME,
    CTF_MODE_UNICODE,
    CTF_PAGE_BITMAP_BYTES,
    CTF_PAGE_SIZE,
    CTF_SUPPORTED_VERSION,
    CTF_TABLE_SIZE,
    CTF_TABLE_TAGS,
    CtfEntry,
    CtfFile,
    CtfHeader,
    CtfL1,
    CtfPage,
    CtfTable,
)
from ttf_parser import TtfFont, crc32_file  # noqa: E402

#: A glyph record cannot describe more than this, and we refuse to emit an
#: offset/length pair that walks off the end of the TTF.
MAX_GLYF_LEN = 0xFFFFFF


# ---------------------------------------------------------------------------
# Building
# ---------------------------------------------------------------------------


def collect_ttf_tables(font: TtfFont) -> list:
    """The table index: tag/offset/length for the tables the firmware needs."""
    out = []
    for tag in CTF_TABLE_TAGS:
        if tag not in font.tables:
            continue
        off, length = font.tables[tag]
        out.append(CtfTable(tag=struct.unpack("<I", tag)[0], offset=off, length=length))
    return out


def make_entry(font: TtfFont, gid: int) -> CtfEntry:
    """Build one CTF entry straight from the parsed TTF."""
    adv, lsb = font.metrics(gid)
    off, length = font.glyph_range(gid)
    header = font.glyph_header(gid)

    flags = CTF_GLYPH_VALID
    x_min = y_min = x_max = y_max = 0

    if header is None or length < 2:
        flags |= CTF_GLYPH_EMPTY
    else:
        ncontours, x_min, y_min, x_max, y_max = header
        if ncontours == 0:
            flags |= CTF_GLYPH_EMPTY
        elif ncontours > 0:
            flags |= CTF_GLYPH_SIMPLE
        else:
            flags |= CTF_GLYPH_COMPOSITE

    if length > MAX_GLYF_LEN:
        raise ValueError("glyph %d too long (%d bytes)" % (gid, length))

    return CtfEntry(
        glyf_offset=off,
        glyf_length=length,
        glyph_id=gid,
        advance_width=adv & 0xFFFF,
        bearing_x=lsb,
        x_min=x_min,
        y_min=y_min,
        x_max=x_max,
        y_max=y_max,
        flags=flags,
    )


def build(ttf_path: str, out_path: str, font_index: int = 0,
          with_crc: bool = True, verbose: bool = False) -> CtfFile:
    """Generate a CTF for ``ttf_path`` and write it to ``out_path``."""
    t0 = time.time()
    with open(ttf_path, "rb") as fh:
        data = fh.read()
    ttf_size = len(data)
    font = TtfFont(data, font_index=font_index)

    cmap = font.cmap()
    if verbose:
        print("  [gen] cmap entries: %d, numGlyphs: %d, unitsPerEm: %d"
              % (len(cmap), font.num_glyphs, font.units_per_em))

    # ---- per code point -> entry, grouped by plane/page -------------------
    # pages[plane][page] = {low: entry}
    pages: dict = {}
    missing = 0
    kinds = {"simple": 0, "composite": 0, "empty": 0}

    for cp in sorted(cmap):
        gid = cmap[cp]
        if gid == 0 or gid >= font.num_glyphs:
            # cmap says "here", but there is no glyph behind it: leave the
            # bit clear so the firmware gets NOT_FOUND without an SD read.
            missing += 1
            continue
        entry = make_entry(font, gid)
        plane = (cp >> 16) & 0xFF
        page_no = (cp >> 8) & 0xFF
        pages.setdefault(plane, {}).setdefault(page_no, {})[cp & 0xFF] = entry
        if entry.is_empty:
            kinds["empty"] += 1
        elif entry.is_composite:
            kinds["composite"] += 1
        else:
            kinds["simple"] += 1

    # ---- lay the file out -------------------------------------------------
    header = CtfHeader(
        ttf_size=ttf_size,
        ttf_crc32=0,
        unicode_mode=CTF_MODE_UNICODE,
        units_per_em=font.units_per_em,
        ascent=font.ascent,
        descent=font.descent,
        line_gap=font.line_gap,
        num_glyphs=font.num_glyphs,
    )

    tables = collect_ttf_tables(font)
    header.table_index_offset = CTF_HEADER_SIZE
    header.table_index_count = len(tables)

    header.l1_index_offset = header.table_index_offset + len(tables) * CTF_TABLE_SIZE
    header.l1_index_count = CTF_L1_ENTRIES
    header.page_index_offset = header.l1_index_offset + CTF_L1_ENTRIES * CTF_L1_SIZE

    # The page array is dense - page_no indexes straight into it - so its
    # length is (highest used page + 1) per plane, gaps included.
    total_pages = sum(max(p) + 1 for p in pages.values() if p)
    header.page_index_count = total_pages
    header.entry_offset = header.page_index_offset + total_pages * CTF_PAGE_SIZE

    # Serialise: L1 records first (they need the page offsets), then pages
    # (they need the running entry offset), then the entry blob.
    l1_records = [CtfL1() for _ in range(CTF_L1_ENTRIES)]
    page_records: list = []
    entry_blob = bytearray()
    entry_count = 0

    page_cursor = header.page_index_offset
    for plane in sorted(pages):
        plane_pages = pages[plane]
        max_page = max(plane_pages)
        l1_records[plane] = CtfL1(page_offset=page_cursor, page_count=max_page + 1)
        for page_no in range(max_page + 1):
            entries = plane_pages.get(page_no, {})
            bitmap = bytearray(CTF_PAGE_BITMAP_BYTES)
            ordered = []
            for low in sorted(entries):
                bitmap[low >> 3] |= 1 << (low & 7)
                ordered.append(entries[low])
            page_records.append(CtfPage(
                entry_offset=header.entry_offset + len(entry_blob),
                entry_count=len(ordered),
                bitmap=bytes(bitmap),
            ))
            for entry in ordered:
                entry_blob += entry.pack()
            entry_count += len(ordered)
            page_cursor += CTF_PAGE_SIZE

    header.entry_count = entry_count
    header.char_count = entry_count

    if kinds["composite"]:
        header.flags |= CTF_FLAG_HAS_COMPOSITE
    if b"kern" in font.tables:
        header.flags |= CTF_FLAG_HAS_KERN
    if b"GPOS" in font.tables:
        header.flags |= CTF_FLAG_HAS_GPOS

    if with_crc:
        header.ttf_crc32 = crc32_file(ttf_path)

    blob = bytearray()
    blob += header.pack()
    for t in tables:
        blob += t.pack()
    for rec in l1_records:
        blob += rec.pack()
    for p in page_records:
        blob += p.pack()
    blob += entry_blob

    with open(out_path, "wb") as fh:
        fh.write(blob)

    elapsed = time.time() - t0
    print("  [gen] %s" % os.path.basename(out_path))
    print("  [gen]   chars=%d (simple=%d composite=%d empty=%d) cmap-missing=%d"
          % (entry_count, kinds["simple"], kinds["composite"], kinds["empty"], missing))
    print("  [gen]   pages=%d  tables=%d  unitsPerEm=%d  upem asc/desc=%d/%d"
          % (total_pages, len(tables), font.units_per_em, font.ascent, font.descent))
    print("  [gen]   file=%d bytes (%.1f KB)  ttf=%d bytes  crc32=0x%08X  %.2fs"
          % (len(blob), len(blob) / 1024.0, ttf_size, header.ttf_crc32, elapsed))

    return CtfFile.load(out_path)


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------


def verify(ctf: CtfFile, ttf_path: str, verbose: bool = False,
           check_absent: bool = True) -> bool:
    """Re-derive every indexed entry from the TTF and compare.

    Also proves the headline property: code points that are *not* in the font
    must resolve to NOT_FOUND through the index alone.
    """
    with open(ttf_path, "rb") as fh:
        data = fh.read()
    font = TtfFont(data)
    cmap = font.cmap()

    ok = True
    checked = 0
    bad = 0

    for cp, have in ctf.iter_unicode():
        checked += 1
        gid = cmap.get(cp, 0)
        if gid == 0 or gid >= font.num_glyphs:
            print("  [ver] FAIL U+%04X indexed but cmap has no glyph" % cp)
            bad += 1
            continue
        want = make_entry(font, gid)
        if (want.glyf_offset, want.glyf_length, want.glyph_id,
                want.advance_width, want.bearing_x,
                want.x_min, want.y_min, want.x_max, want.y_max,
                want.flags) != (
            have.glyf_offset, have.glyf_length, have.glyph_id,
                have.advance_width, have.bearing_x,
                have.x_min, have.y_min, have.x_max, have.y_max,
                have.flags):
            print("  [ver] FAIL U+%04X mismatch" % cp)
            if verbose:
                print("        want glyph=%d off=0x%X len=%d adv=%d flags=0x%X"
                      % (want.glyph_id, want.glyf_offset, want.glyf_length,
                         want.advance_width, want.flags))
                print("        have glyph=%d off=0x%X len=%d adv=%d flags=0x%X"
                      % (have.glyph_id, have.glyf_offset, have.glyf_length,
                         have.advance_width, have.flags))
            bad += 1
            if bad > 10:
                break

    # Bounds: nothing may point outside the TTF.
    for cp, e in ctf.iter_unicode():
        if e.glyf_length and (e.glyf_offset > len(data)
                              or e.glyf_length > len(data) - e.glyf_offset):
            print("  [ver] FAIL U+%04X offset/length outside TTF" % cp)
            bad += 1

    # Every cmap code point with a real glyph must be reachable.
    unreachable = 0
    for cp, gid in cmap.items():
        if gid == 0 or gid >= font.num_glyphs:
            continue
        if ctf.find_unicode(cp) is None:
            unreachable += 1
            if unreachable <= 5:
                print("  [ver] FAIL U+%04X in cmap but NOT_FOUND in CTF" % cp)

    absent_probes = 0
    if check_absent:
        rng = random.Random(0x43544631)
        for _ in range(2000):
            cp = rng.randrange(0x110000)
            if cp in cmap:
                continue
            absent_probes += 1
            if ctf.find_unicode(cp) is not None:
                print("  [ver] FAIL U+%04X absent from font but found in CTF" % cp)
                bad += 1
        for cp in (0x110000, 0x1000000, 0xFFFFFFFF):
            if ctf.find_unicode(cp) is not None:
                print("  [ver] FAIL out-of-range U+%X returned an entry" % cp)
                bad += 1

    ok = bad == 0 and unreachable == 0
    print("  [ver] %s  checked=%d  bad=%d  unreachable=%d  absent-probes=%d"
          % ("PASS" if ok else "FAIL", checked, bad, unreachable, absent_probes))
    return ok


def coverage(ctf: CtfFile) -> None:
    """How much of what the UI actually needs this font can draw."""
    indexed = 0
    ascii_n = 0
    cjk_n = 0
    for cp, _ in ctf.iter_unicode():
        indexed += 1
        if 0x20 <= cp <= 0x7E:
            ascii_n += 1
        if 0x4E00 <= cp <= 0x9FFF:
            cjk_n += 1

    print("  [cov] indexed=%d  ASCII(0x20-0x7E)=%d  CJK(U+4E00-U+9FFF)=%d"
          % (indexed, ascii_n, cjk_n))

    # GB2312 level 1 (the 3755 common simplified characters) is the realistic
    # bar for a Chinese UI: a Traditional-Chinese cut will fall short here.
    gb1_total = 0
    gb1_hit = 0
    for lead in range(0xB0, 0xD8):
        for trail in range(0xA1, 0xFF):
            cp = CtfFile.gbk_to_unicode((lead << 8) | trail)
            if cp is None:
                continue
            gb1_total += 1
            if ctf.find_unicode(cp) is not None:
                gb1_hit += 1
    if gb1_total:
        print("  [cov] GB2312 level-1: %d/%d (%.1f%%)"
              % (gb1_hit, gb1_total, 100.0 * gb1_hit / gb1_total))

    sample = "中文测试你好字库文件系统显示时钟运行缓存温度压力网络设置关于返回菜单图片音乐视频相机串口调试状态"
    miss = [ch for ch in sample if ctf.find_unicode(ord(ch)) is None]
    if miss:
        print("  [cov] UI sample missing %d/%d: %s"
              % (len(miss), len(sample), "".join(miss)))
    else:
        print("  [cov] UI sample: all %d characters present" % len(sample))


def dump(ctf: CtfFile, text: str) -> None:
    """Print the entry behind each character of ``text``."""
    for ch in text:
        cp = ord(ch)
        e = ctf.find_unicode(cp)
        if e is None:
            print("  U+%04X %s  NOT_FOUND" % (cp, repr(ch)))
            continue
        print("  U+%04X %s  glyph=%-6d off=0x%08X len=%-5d adv=%-5d "
              "bbox=(%d,%d)-(%d,%d) %s"
              % (cp, repr(ch), e.glyph_id, e.glyf_offset, e.glyf_length,
                 e.advance_width, e.x_min, e.y_min, e.x_max, e.y_max, e.kind()))


def info(ctf: CtfFile, path: str) -> None:
    h = ctf.header
    print("  [info] %s" % os.path.basename(path))
    print("  [info]   magic=0x%08X version=%d header_size=%d flags=0x%X"
          % (h.magic, h.version, h.header_size, h.flags))
    print("  [info]   mode=%s  units_per_em=%d  num_glyphs=%d"
          % (CTF_MODE_NAME.get(h.unicode_mode, "?"), h.units_per_em, h.num_glyphs))
    print("  [info]   ascent=%d descent=%d line_gap=%d"
          % (h.ascent, h.descent, h.line_gap))
    print("  [info]   ttf_size=%d  ttf_crc32=0x%08X" % (h.ttf_size, h.ttf_crc32))
    print("  [info]   chars=%d  entries=%d  pages=%d  tables=%d"
          % (h.char_count, h.entry_count, h.page_index_count, h.table_index_count))
    for name, off, size in ctf.size_breakdown():
        print("  [info]   %-12s offset=%-8d size=%d" % (name, off, size))
    print("  [info]   tables: %s"
          % ", ".join("%s@%d+%d" % (t.tag_str, t.offset, t.length) for t in ctf.tables))


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Build a CTF index for a TrueType font.")
    ap.add_argument("input", help="source .ttf")
    ap.add_argument("-o", "--output", help="output .ctf (default: alongside input)")
    ap.add_argument("--encoding", default="unicode",
                    choices=["unicode"],
                    help="index encoding (only unicode/multi-level is defined)")
    ap.add_argument("--font-index", type=int, default=0,
                    help="font index inside a .ttc collection")
    ap.add_argument("--verify", action="store_true",
                    help="re-derive every entry from the TTF and compare")
    ap.add_argument("--no-crc", action="store_true",
                    help="skip the CRC32 pass over the TTF (faster)")
    ap.add_argument("--info", action="store_true", help="print header summary")
    ap.add_argument("--dump", metavar="CHARS",
                    help="print entries for these characters")
    ap.add_argument("--coverage", action="store_true",
                    help="report ASCII / CJK / GB2312 coverage")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    if args.encoding != "unicode":
        print("only --encoding unicode is supported in CTF v%d"
              % CTF_SUPPORTED_VERSION, file=sys.stderr)
        return 2

    ttf_path = args.input
    if not os.path.isfile(ttf_path):
        print("no such file: %s" % ttf_path, file=sys.stderr)
        return 2

    out_path = args.output
    if not out_path:
        out_path = os.path.splitext(ttf_path)[0] + ".ctf"

    print("[ttf2ctf] %s" % os.path.basename(ttf_path))

    # Reading an already-built index must not drag the TTF parser in: building
    # a 7 MB font takes minutes and --info/--dump/--coverage only need the
    # .ctf itself.  Pass --verify (or a .ttf input) to force a rebuild.
    inspect_only = (args.info or args.dump or args.coverage) and not args.verify
    if inspect_only and ttf_path.lower().endswith(".ctf"):
        ctf = CtfFile.load(ttf_path)
    else:
        ctf = build(ttf_path, out_path, font_index=args.font_index,
                    with_crc=not args.no_crc, verbose=args.verbose)

    failed = False
    if args.verify:
        failed = not verify(ctf, ttf_path, verbose=args.verbose)
    if args.info:
        info(ctf, out_path)
    if args.coverage:
        coverage(ctf)
    if args.dump:
        dump(ctf, args.dump)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
