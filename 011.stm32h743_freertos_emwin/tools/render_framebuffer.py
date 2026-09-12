#!/usr/bin/env python3
"""
render_framebuffer.py - print gui_vram as ASCII art.

The ST7789 is write-only (no MISO) so there is no way to read the glass back;
dumping `gui_vram` and rendering it here is the practical way to actually *look*
at what the panel is showing.  This is what made the mirrored-glyph bug
obvious: the string "SD / FONT ERROR" rendered as a horizontal mirror
(`/` came out as `\\`, `F` had its stem on the right).

Get a dump first:
    python tools/verify_framebuffer.py                   # dumps build/vram.bin

Then look at it:
    python tools/render_framebuffer.py                       # whole frame
    python tools/render_framebuffer.py -y 6 -Y 24            # the title band
    python tools/render_framebuffer.py -x 50 -X 200 --exclude 0x7A1010
Colors are given as the 0x00RRGGBB literals used in Application/app_ui.c and
are converted the same way the firmware does (ui_col + GUICC_M565 rounding).
"""
from __future__ import annotations

import argparse
import struct
from collections import Counter

W = H = 240


def rgb565(r: int, g: int, b: int) -> int:
    return (((r * 31 + 127) // 255) << 11) | (((g * 63 + 127) // 255) << 5) \
         | ((b * 31 + 127) // 255)


def to565_literal(lit: int) -> int:
    """app_ui.c literal (0x00RRGGBB) -> the word that ends up in gui_vram."""
    v = ((lit & 0xFF) << 16) | (lit & 0xFF00) | ((lit >> 16) & 0xFF)  # ui_col()
    return rgb565(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump-file", default="build/vram.bin")
    ap.add_argument("-x", "--x0", type=int, default=0)
    ap.add_argument("-X", "--x1", type=int, default=W)
    ap.add_argument("-y", "--y0", type=int, default=0)
    ap.add_argument("-Y", "--y1", type=int, default=H)
    ap.add_argument("--exclude", default="",
                    help="comma-separated 0xRRGGBB literals to treat as background")
    ap.add_argument("--exclude-auto", type=int, default=1,
                    help="also treat the N most common colours in the region as "
                         "background (default 1 = the page background)")
    ap.add_argument("-s", "--scale-x", type=int, default=2,
                    help="horizontal downsample factor (2 halves the width)")
    args = ap.parse_args()

    raw = open(args.dump_file, "rb").read()
    if len(raw) != W * H * 2:
        print(f"{args.dump_file}: expected {W * H * 2} bytes, got {len(raw)}")
        return 1
    px = struct.unpack("<%dH" % (W * H), raw)

    region = [px[y * W + x] for y in range(args.y0, min(args.y1, H))
              for x in range(args.x0, min(args.x1, W))]

    bg = set()
    for item in args.exclude.split(","):
        item = item.strip()
        if item:
            bg.add(to565_literal(int(item, 16)))
    if args.exclude_auto > 0:
        bg.update(v for v, _ in Counter(region).most_common(args.exclude_auto))

    print(f"# {args.dump_file}  x[{args.x0}..{args.x1}) y[{args.y0}..{args.y1})  "
          f"background={{{', '.join('0x%04X' % v for v in sorted(bg))}}}")
    print(f"# legend: '#' = lit, '.' = background, '·' = merged(scale-x) partial")

    for y in range(args.y0, min(args.y1, H)):
        line = []
        for x in range(args.x0, min(args.x1, W)):
            hits = sum(1 for xx in range(x, min(x + args.scale_x, W))
                       if px[y * W + xx] not in bg)
            if hits == 0:
                line.append(".")
            elif hits == args.scale_x:
                line.append("#")
            else:
                line.append("·")
        print("".join(line))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
