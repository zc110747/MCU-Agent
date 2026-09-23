"""Acceptance check for the *shapes* the card-side GBK face draws.

Why this exists
---------------
verify_sd_font.py proves the index arithmetic is self-consistent: the file is
23940 glyphs long, hzk_index() stays in range, no two GBK pairs collide, and
every codepoint the firmware claims to map really exists.  All of that can be
true while the glyph at slot N is a *different character* - the arithmetic only
says "this cell exists", never "this cell is U+4E2D".

That failure is invisible from the inside.  The font still answers
`found = 1`, the box is still 16x16, the advance is still 16 px, the label still
lays out and the page still paginates.  The text is simply the wrong words,
which is what "the Chinese is garbled" looks like from the outside.

So this script takes the one piece of ground truth that comes off the card -
the bitmaps the firmware printed, drawn through the production path
(Theme::font_cjk() -> lv_font_get_glyph_dsc -> lv_font_get_glyph_bitmap) -
and compares them against an independent renderer on the host.

The two sources
---------------
  * the device log      ASCII art from the boot probe, one block per codepoint
  * SimSun on this host  Windows' own 宋体, requested at exactly 16 px so
                        FreeType uses the font's embedded 16 px bitmap strike.
                        HZK16 *is* the 宋体 16 px dot matrix, which is what makes
                        the two comparable at all - they are two renderings of
                        one design, not two unrelated typefaces.

How the score is read
---------------------
The two renderings do not place the glyph at the same pixel: they are the same
design rasterised by two different producers, so strokes sit 0-1 px apart.  A
strict overlap therefore caps out around 0.7 even for a perfect glyph - which is
useless as a pass mark.  The score used here is tolerant: a pixel of one bitmap
counts as matched when the other bitmap has ink within one pixel of it, counted
in both directions and normalised by total ink.  That is ~1.0 for the right
character and stays low for a different one, which is the only judgement this
script is making: right character or wrong character, not typographic identity.

Measured on this card's GBK16.FON, before and after sd_font.cpp was corrected
from a row-major to a column-major read:

    codepoint      row-major (wrong)   column-major (right)
    U+5728 在           0.23                 0.97
    U+5FEB 快           0.26                 0.92
    U+901F 速           0.29                 0.94
    U+53D1 发           0.18                 0.86

Usage
-----
    python tools/verify_glyph_shapes.py [serial_log.txt]

Without an argument it looks for serial_log.txt, then serial_cjk2.txt.

The input comes from the temporary probe that used to sit at the end of
main.cpp's app_main(); it is removed from shipped firmware on purpose.  To
regenerate it, restore that block (git log main/main.cpp) and capture a boot
with tools/serial_probe.py.
"""

import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOGS = ("serial_log.txt", "serial_cjk2.txt")

# The 宋体 face the HZK16 dot matrix was designed from.  16 px exactly: any
# other size gets FreeType's outline scaler instead of the embedded strike, and
# the comparison stops being meaningful.
REF_FONT = Path("C:/Windows/Fonts/simsun.ttc")
REF_SIZE = 16

# A tolerant score is ~1.0 for the right character and below ~0.65 for a
# different one, so the bands are set where nothing observed lands.
PASS_SCORE = 0.85
FAIL_SCORE = 0.70
SEARCH = 3          # offsets tried, in px

CANVAS = 24

GLYPH_RE = re.compile(r"PROBE glyph U\+([0-9A-Fa-f]{4,6}) found=(\d+) box=(\d+)x(\d+)")
ART_RE = re.compile(r"PROBE {3}\|([.#]+)\|")

results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print("  [%s] %-44s %s" % ("PASS" if ok else "FAIL", name, detail))
    return ok


def parse_device_art(log_text):
    """[{cp, box, rows}] - one entry per glyph block in the log."""
    blocks, current = [], None
    for line in log_text.splitlines():
        m = GLYPH_RE.search(line)
        if m:
            if current is not None and current["rows"]:
                blocks.append(current)
            current = {"cp": int(m.group(1), 16), "found": int(m.group(2)),
                       "box": (int(m.group(3)), int(m.group(4))), "rows": []}
            continue
        if current is None:
            continue
        a = ART_RE.search(line)
        if a:
            current["rows"].append(a.group(1))
        elif current["rows"]:
            blocks.append(current)
            current = None
    if current is not None and current["rows"]:
        blocks.append(current)
    return blocks


def center(a):
    """Ink-cropped, then centred on a fixed canvas so offsets are comparable."""
    ys, xs = np.nonzero(a)
    if len(ys) == 0:
        return None
    a = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    c = np.zeros((CANVAS, CANVAS), bool)
    h, w = a.shape
    oy, ox = (CANVAS - h) // 2, (CANVAS - w) // 2
    c[oy:oy + h, ox:ox + w] = a
    return c


def device_bitmap(block):
    w, h = block["box"]
    art = np.zeros((h, w), bool)
    for y, row in enumerate(block["rows"][:h]):
        for x, ch in enumerate(row[:w]):
            art[y, x] = (ch == "#")
    return center(art)


def reference_bitmap(cp):
    font = ImageFont.truetype(str(REF_FONT), REF_SIZE)
    canvas = Image.new("L", (64, 64), 0)
    ImageDraw.Draw(canvas).text((16, 16), chr(cp), fill=255, font=font)
    if canvas.getbbox() is None:
        return None
    g = canvas.crop(canvas.getbbox())
    return center(np.asarray(g) > 127)


def dilate(a):
    b = a.copy()
    b[1:, :] |= a[:-1, :]
    b[:-1, :] |= a[1:, :]
    b[:, 1:] |= a[:, :-1]
    b[:, :-1] |= a[:, 1:]
    return b


def match(dev, ref):
    """Best tolerant agreement over small placements, and the strict IoU there.

    Tolerant: ink of one bitmap that has ink of the other within 1 px counts as
    matched.  Both directions are counted, so a missing stroke is penalised as
    much as an extra one and a glyph cannot score well by being sparse.
    """
    best, strict_best = 0.0, 0.0
    dev_ink = int(dev.sum())
    dev_d = dilate(dev)
    for dy in range(-SEARCH, SEARCH + 1):
        for dx in range(-SEARCH, SEARCH + 1):
            r = np.roll(np.roll(ref, dy, 0), dx, 1)
            r_ink = int(r.sum())
            if dev_ink == 0 or r_ink == 0:
                continue
            r_d = dilate(r)
            score = (int((dev & r_d).sum()) + int((r & dev_d).sum())) / float(dev_ink + r_ink)
            if score > best:
                union = int((dev | r).sum())
                best = score
                strict_best = int((dev & r).sum()) / float(union) if union else 0.0
    return best, strict_best


def main(argv):
    log_path = None
    for cand in (argv[1:2] or DEFAULT_LOGS):
        p = Path(cand)
        p = p if p.is_absolute() else ROOT / p
        if p.is_file():
            log_path = p
            break

    if log_path is None:
        print("no device log found in %s" % ", ".join(DEFAULT_LOGS))
        return 1

    raw = log_path.read_bytes().replace(b"\x00", b"")
    raw = re.sub(rb"\x1b\[[0-9;]*[a-zA-Z]", b"", raw).decode("utf-8", "replace")
    print("device log : %s" % log_path.name)
    print("reference  : %s @ %d px" % (REF_FONT.name, REF_SIZE))
    print()

    blocks = [b for b in parse_device_art(raw) if b["found"] and b["rows"]]
    if not check("log carries glyph bitmaps to compare", len(blocks) > 0,
                 "%d glyph blocks" % len(blocks)):
        return 1

    weak = []
    for b in blocks:
        cp = b["cp"]
        dev = device_bitmap(b)
        ref = reference_bitmap(cp)
        if dev is None or ref is None:
            check("U+%04X %s" % (cp, chr(cp)), False,
                  "no ink on one side (device %s, host %s)"
                  % ("empty" if dev is None else "has ink",
                     "empty" if ref is None else "has ink"))
            continue
        score, strict = match(dev, ref)
        ok = score >= PASS_SCORE
        check("U+%04X %s is drawn as itself" % (cp, chr(cp)), ok,
              "%.2f tolerant / %.2f exact" % (score, strict))
        if FAIL_SCORE <= score < PASS_SCORE:
            weak.append((cp, score))

    print()
    if weak:
        print("borderline - the character is probably right, the rendering differs:")
        for cp, score in weak:
            print("  U+%04X %s  %.2f" % (cp, chr(cp), score))
        print()

    passed = sum(1 for _, ok, _ in results if ok)
    print("%d/%d checks passed" % (passed, len(results)))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
