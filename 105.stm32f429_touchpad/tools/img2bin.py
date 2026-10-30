#!/usr/bin/env python3
"""
img2bin.py - convert an image into an LVGL v8 binary image (.bin) for
             105.stm32f429_touchpad.

The firmware draws wallpapers and icons straight from the microSD card with
LVGL's built-in image decoder, so the files must be in LVGL's *binary* format,
not PNG/JPG.  This script writes exactly what the decoder expects:

    offset 0   uint32   header: cf[0:5] | w[10:21] | h[21:32]  (little endian)
    offset 4   uint16[] RGB565 pixels, row major, little endian

Usage
-----
    python tools/img2bin.py wallpaper.png  sd/IMG/bg.bin --size 800x480
    python tools/img2bin.py logo.png       sd/IMG/logo.bin

Card layout expected by the firmware:

    0:/IMG/bg.bin     800x480 wallpaper (optional)
    0:/IMG/<name>.bin any icon; its size comes from the header

Requires Pillow (pip install pillow).
"""

import argparse
import struct
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("Pillow is required: pip install pillow")

LV_IMG_CF_TRUE_COLOR = 4


def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def build_header(width, height, cf=LV_IMG_CF_TRUE_COLOR):
    if not (0 < width <= 2047 and 0 < height <= 2047):
        raise ValueError("image must be 1..2047 px in each direction")
    return cf | (width << 10) | (height << 21)


def convert(src, dst, size=None, fit=False, background=(0, 0, 0)):
    img = Image.open(src)

    # Alpha is composited onto a solid background: RGB565 has no alpha channel.
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        flat = Image.new("RGBA", img.size, background + (255,))
        img = Image.alpha_composite(flat, img)
    img = img.convert("RGB")

    if size is not None:
        if fit:
            canvas = Image.new("RGB", size, background)
            copy = img.copy()
            copy.thumbnail(size, Image.LANCZOS)
            canvas.paste(copy, ((size[0] - copy.width) // 2,
                                (size[1] - copy.height) // 2))
            img = canvas
        else:
            img = img.resize(size, Image.LANCZOS)

    width, height = img.size
    pixels = img.load()

    out = bytearray(struct.pack("<I", build_header(width, height)))
    for y in range(height):
        for x in range(width):
            out += struct.pack("<H", rgb888_to_rgb565(*pixels[x, y]))

    with open(dst, "wb") as fh:
        fh.write(out)

    print("%s -> %s  %dx%d  %d bytes" % (src, dst, width, height, len(out)))


def parse_size(text):
    try:
        w, h = text.lower().split("x")
        return int(w), int(h)
    except Exception:
        raise argparse.ArgumentTypeError("use WIDTHxHEIGHT, e.g. 800x480")


def main():
    ap = argparse.ArgumentParser(description="image -> LVGL v8 binary image")
    ap.add_argument("input", help="source image (png/jpg/bmp/...)")
    ap.add_argument("output", help="destination .bin")
    ap.add_argument("--size", type=parse_size, default=None,
                    help="resize to WIDTHxHEIGHT (default: keep the source size)")
    ap.add_argument("--fit", action="store_true",
                    help="letterbox into --size instead of stretching")
    args = ap.parse_args()

    if args.output.lower().endswith(".bin") is False:
        print("warning: LVGL only accepts *.bin (lower case) on the card",
              file=sys.stderr)

    convert(args.input, args.output, args.size, args.fit)


if __name__ == "__main__":
    main()
