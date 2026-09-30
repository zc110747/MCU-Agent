#!/usr/bin/env python3
"""Recolour app/menu_icons.c in place for the dark watch-face theme.

The icon bitmaps were drawn for the light theme: white background, dark-blue
glyph (RGB565 0xFFFF / 0x0319), with anti-aliased edge shades in between.
For the dark UI every 48x48 app icon is re-baked on its per-app chip colour
with a WHITE glyph, so the square bitmap melts into the filled menu circle
(see s_chip[] in app_menu.c).  The 16x16 folder icon becomes a blue chip.

Anti-aliasing is preserved: every source pixel is a linear RGB blend between
the dark glyph colour D and white W.  Its ink amount is recovered exactly via

    a = (lum(W) - lum(p)) / (lum(W) - lum(D))      (clamped to [0,1])

and the output pixel is the inverse blend

    out = a*WHITE + (1-a)*ACCENT                   (per 8-bit channel)

so pure glyph -> white, background -> chip colour, edges stay smooth.
Only pixel colours change: array names, sizes, byte-per-line layout and the
lv_img_dsc_t descriptors are preserved byte-for-byte.  (LV_COLOR_16_SWAP=0,
pixels stored little-endian - verified in third_party/lvgl/lv_conf.h.)
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "app" / "menu_icons.c"

GLYPH_OLD = 0x0319   # dark-blue glyph on the light theme
BG_OLD    = 0xFFFF   # white background on the light theme
GLYPH_NEW = 0xFFFF   # white glyph on the dark theme

# Per-icon chip colours (24-bit RGB), same table as s_chip[] in app_menu.c.
ACCENT_888 = {
    "clock":   0xFF9F0A,   # orange
    "camera":  0x32ADE6,   # cyan
    "txt":     0x0A84FF,   # blue
    "image":   0xBF5AF2,   # purple
    "nes":     0xFF453A,   # red
    "keytest": 0xFFD60A,   # yellow
    "sysinfo": 0x30D158,   # green
    "about":   0xFF2D78,   # pink
    "folder":  0x0A84FF,   # blue (browser directory chip)
}


def c565_to_888(v: int) -> tuple[int, int, int]:
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def c888_to_565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def lum(rgb: tuple[int, int, int]) -> float:
    return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]


LUM_WHITE  = 255.0
LUM_INK    = lum(c565_to_888(GLYPH_OLD))
INK_SPAN   = LUM_WHITE - LUM_INK
INK_888    = c565_to_888(GLYPH_OLD)

ACCENT_565 = {k: c888_to_565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)
              for k, v in ACCENT_888.items()}


def remap_pixel(p: int, accent888: tuple[int, int, int]) -> int:
    a = (LUM_WHITE - lum(c565_to_888(p))) / INK_SPAN
    a = 0.0 if a < 0.0 else (1.0 if a > 1.0 else a)
    r = round(a * 255.0 + (1.0 - a) * accent888[0])
    g = round(a * 255.0 + (1.0 - a) * accent888[1])
    b = round(a * 255.0 + (1.0 - a) * accent888[2])
    return c888_to_565(r, g, b)


ARRAY_RE = re.compile(
    r"(static const uint8_t g_icon_(\w+)\[(\d+)\] =\s*\{)(.*?)(\};)", re.S)
BYTE_RE = re.compile(r"0x[0-9A-Fa-f]{2}")


def recolor(name: str, decl_size: int, body: str, dry: bool) -> str:
    """Substitute the hex byte tokens inside one array body in place.

    The original text layout (CRLF endings, bytes-per-line width, trailing
    commas, indentation) is preserved token-for-token; only the two hex
    digits of every byte change.
    """
    tokens = BYTE_RE.findall(body)
    if len(tokens) != decl_size:
        raise SystemExit(f"ERROR: {name}: {len(tokens)} bytes parsed, "
                         f"declared {decl_size}")
    if decl_size % 2:
        raise SystemExit(f"ERROR: {name}: odd byte count")

    raw = [int(v, 16) for v in tokens]
    pixels = [raw[i] | (raw[i + 1] << 8) for i in range(0, len(raw), 2)]

    uniq = sorted(set(pixels))
    hist = [(f"0x{p:04X}", pixels.count(p)) for p in uniq]
    print(f"{name:8s} uniq={len(uniq)}: "
          + ", ".join(f"{c}x{v}" for v, c in hist))
    if BG_OLD not in uniq:
        raise SystemExit(f"ERROR: {name}: no white background pixels found - "
                         f"icon layout not light-theme, aborting")

    if dry:
        return body

    accent888 = c565_to_888(ACCENT_565[name])
    new_bytes = []
    for p in pixels:
        np = remap_pixel(p, accent888)
        new_bytes.append(np & 0xFF)
        new_bytes.append((np >> 8) & 0xFF)

    it = iter(new_bytes)
    return BYTE_RE.sub(lambda _m: f"0x{next(it):02X}", body)


def main() -> int:
    dry = "--dry" in sys.argv[1:]
    # newline="" on BOTH ends: read keeps CRLF in the string, write saves
    # them back byte-for-byte (read_text() would silently translate to LF).
    with open(SRC, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    reports = []

    def repl(m: "re.Match[str]") -> str:
        head, name, size, body, tail = m.groups()
        reports.append(name)
        # head already ends with "{" (its trailing newline was consumed by
        # \s*), body starts with the newline after "{" - join directly.
        return head + recolor(name, int(size), body, dry) + tail

    new_text = ARRAY_RE.sub(repl, text)

    if sorted(reports) != sorted(ACCENT_565):
        raise SystemExit(f"ERROR: icons found {sorted(reports)} != "
                         f"expected {sorted(ACCENT_565)}, aborting")

    if dry:
        print("dry run: file untouched")
        return 0

    # newline="": keep the file's own CRLF endings byte-for-byte
    SRC.write_text(new_text, encoding="utf-8", newline="")

    print(f"ink D=0x{GLYPH_OLD:04X} lum={LUM_INK:.1f}, "
          f"bg W=0x{BG_OLD:04X} -> per-app accent")
    for name in reports:
        print(f"{name:8s}: bg 0x{BG_OLD:04X} -> 0x{ACCENT_565[name]:04X} "
              f"(#{ACCENT_888[name]:06X}), glyph -> 0x{GLYPH_NEW:04X}")
    print(f"OK: {len(reports)} icons recoloured in {SRC}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
