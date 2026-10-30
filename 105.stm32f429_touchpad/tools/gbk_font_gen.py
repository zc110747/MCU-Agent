#!/usr/bin/env python3
"""
gbk_font_gen.py - build the SD-card GBK glyph files used by
                  105.stm32f429_touchpad.

The firmware keeps every Chinese glyph on the card (flash holds ASCII only) and
reads one glyph at a time with f_lseek + f_read.  The layout it expects is the
classic 正点原子 GBKxx.FON:

    file    box      bytes/glyph     index
    GBK12   12x12    24              190 * (lead - 0x81) + (trail - 0x40 | 0x41)
    GBK16   16x16    32
    GBK24   24x24    72
    GBK32   32x32    128

    glyph   MSB first, COLUMN scan, ceil(box/8) bytes per column

Usage
-----
    python tools/gbk_font_gen.py C:/Windows/Fonts/msyh.ttc out/
    python tools/gbk_font_gen.py myfont.ttf out/ --sizes 16 24
    python tools/gbk_font_gen.py myfont.ttf out/ --leads 0xB0 0xB1   # smoke test

Copy the result to the card:

    0:/SYSTEM/FONT/GBK12.FON
    0:/SYSTEM/FONT/GBK16.FON
    0:/SYSTEM/FONT/GBK24.FON
    0:/SYSTEM/FONT/GBK32.FON

Requires Pillow (pip install pillow).
"""

import argparse
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover
    sys.exit("Pillow is required: pip install pillow")

TRAILS_PER_LEAD = 190
LEAD_FIRST = 0x81
LEAD_LAST = 0xFE
THRESHOLD = 128


def glyph_bytes(box):
    return ((box + 7) // 8) * box


def render_glyph(ch, font, box):
    """Render one character into a box x box binary mask, centred."""
    img = Image.new("L", (box, box), 0)
    draw = ImageDraw.Draw(img)

    try:
        left, top, right, bottom = draw.textbbox((0, 0), ch, font=font)
    except Exception:
        return img

    gw = right - left
    gh = bottom - top
    ox = (box - gw) // 2 - left
    oy = (box - gh) // 2 - top
    draw.text((ox, oy), ch, fill=255, font=font)
    return img


def pack_column_msb(img, box):
    """MSB-first, column-major packing (the format the driver reads)."""
    px = img.load()
    out = bytearray()
    bytes_per_col = (box + 7) // 8

    for x in range(box):
        for blk in range(bytes_per_col):
            value = 0
            for bit in range(8):
                y = blk * 8 + bit
                if y < box and px[x, y] >= THRESHOLD:
                    value |= 0x80 >> bit
            out.append(value)
    return bytes(out)


def build_size(font_path, size, out_dir, leads):
    box = size
    font = ImageFont.truetype(font_path, size)
    per_glyph = glyph_bytes(box)
    blank = bytes(per_glyph)

    name = os.path.join(out_dir, "GBK%d.FON" % size)
    total = 0
    with open(name, "wb") as fh:
        for lead in range(LEAD_FIRST, LEAD_LAST + 1):
            for trail in range(0x40, 0xFF):
                if trail == 0x7F:
                    continue
                if leads is not None and lead not in leads:
                    fh.write(blank)
                    continue
                try:
                    ch = bytes((lead, trail)).decode("gbk")
                except UnicodeDecodeError:
                    fh.write(blank)
                    continue

                try:
                    img = render_glyph(ch, font, box)
                    fh.write(pack_column_msb(img, box))
                except Exception:
                    fh.write(blank)
                total += 1
    print("%s: %d glyphs rendered, %d bytes" % (name, total, os.path.getsize(name)))
    return name


def main():
    ap = argparse.ArgumentParser(description="build GBKxx.FON glyph files")
    ap.add_argument("ttf", help="source TrueType/OpenType font")
    ap.add_argument("outdir", help="directory for GBK12/16/24/32.FON")
    ap.add_argument("--sizes", type=int, nargs="+", default=[12, 16, 24, 32],
                    help="box sizes to build (default: 12 16 24 32)")
    ap.add_argument("--leads", type=lambda v: int(v, 0), nargs=2,
                    metavar=("FIRST", "LAST"),
                    help="only render lead bytes FIRST..LAST (smoke test)")
    args = ap.parse_args()

    if not os.path.isfile(args.ttf):
        sys.exit("font not found: %s" % args.ttf)
    os.makedirs(args.outdir, exist_ok=True)

    leads = None
    if args.leads is not None:
        leads = set(range(args.leads[0], args.leads[1] + 1))

    for size in args.sizes:
        if size not in (12, 16, 24, 32):
            print("warning: %d is not one of the four sizes the firmware opens"
                  % size, file=sys.stderr)
        build_size(args.ttf, size, args.outdir, leads)


if __name__ == "__main__":
    main()
