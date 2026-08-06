#!/usr/bin/env python3
"""
Convert a raw RGB565 frame dump (as captured from the target's frame buffer
with GDB `dump binary memory`) into a PNG, and print a sanity report.

Usage:  python rgb565_to_png.py <in.bin> <out.png> [width] [height]

No third-party dependencies: PNG is emitted with zlib + struct only.
"""
import sys
import zlib
import struct


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path: str, w: int, h: int, rgb: bytes) -> None:
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)                       # filter type 0 (None)
        raw += rgb[y * stride:(y + 1) * stride]

    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += png_chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    src, dst = sys.argv[1], sys.argv[2]
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 240
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 240

    data = open(src, "rb").read()
    want = w * h * 2
    if len(data) != want:
        print(f"! size mismatch: got {len(data)}, expected {want}")
        return 2

    rgb = bytearray(w * h * 3)
    hist = {}
    dark = bright = 0

    for i in range(w * h):
        v = data[2 * i] | (data[2 * i + 1] << 8)     # little-endian uint16
        r5 = (v >> 11) & 0x1F
        g6 = (v >> 5) & 0x3F
        b5 = v & 0x1F
        # 5/6-bit -> 8-bit with proper bit replication
        r = (r5 << 3) | (r5 >> 2)
        g = (g6 << 2) | (g6 >> 4)
        b = (b5 << 3) | (b5 >> 2)
        rgb[3 * i + 0] = r
        rgb[3 * i + 1] = g
        rgb[3 * i + 2] = b

        hist[v] = hist.get(v, 0) + 1
        lum = (r * 299 + g * 587 + b * 114) // 1000
        if lum < 16:
            dark += 1
        elif lum > 239:
            bright += 1

    write_png(dst, w, h, bytes(rgb))

    total = w * h
    top = sorted(hist.items(), key=lambda kv: -kv[1])[:3]
    print(f"  {dst}")
    print(f"    unique colours : {len(hist)} / {total}")
    print(f"    near-black px  : {dark} ({100.0 * dark / total:.1f}%)")
    print(f"    near-white px  : {bright} ({100.0 * bright / total:.1f}%)")
    print("    top colours    : "
          + ", ".join(f"0x{v:04x}x{n}" for v, n in top))
    return 0


if __name__ == "__main__":
    sys.exit(main())
