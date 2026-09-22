"""Generate the application's icon set as LVGL ARGB8888 image descriptors.

Why not the built-in symbol font
--------------------------------
LVGL's bundled Montserrat fonts carry only 60 Font Awesome glyphs, and that set
has no clock, no calendar, no cloud and no pencil -- exactly four of the nine
Home tiles.  Rather than mix "real icon" for five tiles with "something vaguely
similar" for the other four, every icon is drawn here so the set is visually
consistent.

Why not Pillow / cairosvg
-------------------------
Neither is installed, and generating the C array from a rasteriser we control
keeps the shapes reproducible.  The rasteriser is a signed-distance-field one:
each primitive exposes a function returning the distance from a point to the
shape's boundary (negative inside), and a single coverage rule turns that into
an antialiased alpha:

    stroke of half-width w   alpha = clamp(0.5 + w - d, 0, 1)
    filled shape             alpha = clamp(0.5 - d, 0, 1)

which is exact enough at these sizes and needs no supersampling.

Output
------
Icons are drawn white with an alpha channel and are tinted at runtime through
LVGL's image recolor, so a theme change never requires regenerating them.
"""

import math
import pathlib
import sys

# --------------------------------------------------------------------------
# coverage helpers
# --------------------------------------------------------------------------


def clamp01(v):
    return 0.0 if v < 0.0 else (1.0 if v > 1.0 else v)


def seg_dist(px, py, ax, ay, bx, by):
    """Distance from (px,py) to the segment (ax,ay)-(bx,by)."""
    vx, vy = bx - ax, by - ay
    wx, wy = px - ax, py - ay
    l2 = vx * vx + vy * vy
    t = 0.0 if l2 == 0.0 else max(0.0, min(1.0, (wx * vx + wy * vy) / l2))
    return math.hypot(px - (ax + t * vx), py - (ay + t * vy))


# --- primitive signed-distance functions ----------------------------------


def ring(cx, cy, r):
    return lambda x, y: abs(math.hypot(x - cx, y - cy) - r)


def disc(cx, cy, r):
    return lambda x, y: math.hypot(x - cx, y - cy) - r


def seg(ax, ay, bx, by):
    return lambda x, y: seg_dist(x, y, ax, ay, bx, by)


def rrect(cx, cy, hw, hh, r):
    def f(x, y):
        qx = abs(x - cx) - (hw - r)
        qy = abs(y - cy) - (hh - r)
        return (math.hypot(max(qx, 0.0), max(qy, 0.0))
                + min(max(qx, qy), 0.0) - r)
    return f


def arc(cx, cy, r, a0_deg, a1_deg):
    """Distance to a circular arc; angles measured clockwise from +X."""
    a0 = math.radians(a0_deg)
    a1 = math.radians(a1_deg)

    def f(x, y):
        dx, dy = x - cx, y - cy
        ang = math.atan2(dy, dx)
        # normalise into [a0, a0+2pi)
        while ang < a0:
            ang += 2 * math.pi
        if ang <= a1:
            return abs(math.hypot(dx, dy) - r)
        # outside the sweep: nearest end point
        p0 = (cx + r * math.cos(a0), cy + r * math.sin(a0))
        p1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))
        return min(math.hypot(x - p0[0], y - p0[1]),
                   math.hypot(x - p1[0], y - p1[1]))

    return f


def convex_poly(points):
    """Signed distance to a convex polygon, negative inside.

    Deliberately NOT the usual `max(distance to each edge line)` formulation:
    that only holds inside the half-plane intersection, and a point out on the
    extension of an edge gets a spuriously negative value there.  For a fill
    that mistake floods the whole tile.  An explicit inside test plus the
    distance to the nearest edge *segment* is correct everywhere.

    `points` may be given in either winding order; the interior sign is taken
    from the centroid so callers never have to think about it.
    """
    n = len(points)
    cx = sum(p[0] for p in points) / n
    cy = sum(p[1] for p in points) / n

    sign = 1.0
    for i in range(n):
        ax, ay = points[i]
        bx, by = points[(i + 1) % n]
        cr = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
        if abs(cr) > 1e-9:
            sign = 1.0 if cr > 0.0 else -1.0
            break

    def f(x, y):
        inside = True
        nearest = 1e18
        for i in range(n):
            ax, ay = points[i]
            bx, by = points[(i + 1) % n]
            if ((bx - ax) * (y - ay) - (by - ay) * (x - ax)) * sign < 0.0:
                inside = False
            d = seg_dist(x, y, ax, ay, bx, by)
            if d < nearest:
                nearest = d
        return -nearest if inside else nearest

    return f


class Canvas:
    """Alpha-only canvas; primitives union together by max()."""

    def __init__(self, size):
        self.n = size
        self.alpha = [0.0] * (size * size)

    def stroke(self, sd, width):
        """Paint a stroke of `width` centred on the shape's boundary.

        The coverage is |d| based on purpose: only the distance to the boundary
        matters for a stroke, so the inside of a closed shape must not fill up.
        Using `half - d` instead would flood every rectangle solid, because d
        is strongly negative anywhere in the interior.
        """
        half = width * 0.5
        n, acc = self.n, self.alpha
        for py in range(n):
            yy = py + 0.5
            row = py * n
            for px in range(n):
                d = sd(px + 0.5, yy)
                if d < 0.0:
                    d = -d
                a = half - d + 0.5
                if a > 0.0:
                    if a > 1.0:
                        a = 1.0
                    k = row + px
                    if a > acc[k]:
                        acc[k] = a

    def fill(self, sd):
        n, acc = self.n, self.alpha
        for py in range(n):
            yy = py + 0.5
            row = py * n
            for px in range(n):
                d = sd(px + 0.5, yy)
                a = 0.5 - d
                if a > 0.0:
                    if a > 1.0:
                        a = 1.0
                    k = row + px
                    if a > acc[k]:
                        acc[k] = a

    def polyline(self, points, width):
        for i in range(len(points) - 1):
            (ax, ay), (bx, by) = points[i], points[i + 1]
            self.stroke(seg(ax, ay, bx, by), width)

    def to_argb(self):
        """White pixels, alpha from the coverage.  LVGL ARGB8888 is B,G,R,A."""
        out = []
        for a in self.alpha:
            v = int(round(a * 255.0))
            out.append((v << 24) | 0x00FFFFFF)   # A=alpha, R=G=B=255
        return out


# --------------------------------------------------------------------------
# icon definitions  -- 48x48 tiles, 3.2 px strokes, drawn on a 48 px grid
# --------------------------------------------------------------------------

APP = 48
STROKE = 3.2


def icon_reader(c):
    c.stroke(rrect(23.5, 24.0, 18.0, 15.0, 3.0), STROKE)      # cover
    c.stroke(seg(23.5, 9.6, 23.5, 38.4), STROKE)               # spine
    c.stroke(seg(11.0, 17.0, 19.0, 17.0), 2.2)                 # lines on page
    c.stroke(seg(11.0, 23.5, 19.0, 23.5), 2.2)
    c.stroke(seg(28.0, 17.0, 36.0, 17.0), 2.2)
    c.stroke(seg(28.0, 23.5, 36.0, 23.5), 2.2)


def icon_photos(c):
    c.stroke(rrect(23.5, 24.0, 18.0, 14.5, 3.0), STROKE)       # frame
    c.fill(disc(32.0, 17.0, 3.4))                              # sun
    c.polyline([(8.0, 35.0), (17.5, 23.0), (25.0, 32.0), (30.5, 26.0), (39.0, 35.0)], 2.6)


def icon_notes(c):
    c.stroke(rrect(23.5, 24.0, 14.0, 17.0, 3.0), STROKE)
    for y in (16.5, 23.5, 30.5):
        c.stroke(seg(15.0, y, 32.0, y), 2.2)
    c.stroke(seg(15.0, 37.0, 25.0, 37.0), 2.2)


def icon_weather(c):
    c.fill(disc(16.5, 28.0, 7.6))                              # cloud lobes
    c.fill(disc(25.0, 25.5, 8.6))
    c.fill(disc(33.0, 29.0, 6.6))
    c.fill(rrect(24.0, 32.5, 12.0, 3.4, 1.6))
    c.stroke(ring(37.5, 12.5, 5.0), 2.6)                       # sun behind


def icon_clock(c):
    c.stroke(ring(24.0, 24.0, 16.5), STROKE)
    c.stroke(seg(24.0, 24.0, 24.0, 13.5), 2.6)                 # hour hand
    c.stroke(seg(24.0, 24.0, 32.0, 28.5), 2.6)                 # minute hand


def icon_calendar(c):
    c.stroke(rrect(23.5, 25.0, 17.5, 15.0, 3.0), STROKE)
    c.stroke(seg(6.0, 17.5, 41.0, 17.5), 2.2)                  # header rule
    c.stroke(seg(14.0, 6.0, 14.0, 13.0), 2.6)                  # binding rings
    c.stroke(seg(33.0, 6.0, 33.0, 13.0), 2.6)
    for x in (15.0, 24.0, 33.0):
        for y in (25.0, 33.0):
            c.fill(disc(x, y, 2.0))


def icon_drawing(c):
    # Fat round-capped stroke for the body, then a polygon for the sharpened
    # tip: a second, thinner stroke reads as a second pencil, not a taper.
    c.stroke(seg(14.0, 33.0, 30.0, 17.0), 7.4)
    c.fill(convex_poly([(32.6, 19.6), (27.4, 14.4), (36.2, 10.8)]))
    # Two short strokes underneath, so the tile reads as "drawing" rather
    # than just "pencil".
    c.stroke(seg(10.0, 40.5, 22.0, 40.5), 2.4)
    c.stroke(seg(26.5, 40.5, 38.0, 40.5), 2.4)


def icon_files(c):
    c.polyline([(6.0, 38.0), (6.0, 12.0), (18.0, 12.0), (22.0, 17.0), (42.0, 17.0)], STROKE)
    c.stroke(rrect(24.0, 29.5, 18.0, 11.0, 3.0), STROKE)


def icon_settings(c):
    c.stroke(ring(24.0, 24.0, 9.0), 3.4)
    for i in range(8):
        a = math.radians(i * 45.0)
        c.stroke(seg(24.0 + 13.0 * math.cos(a), 24.0 + 13.0 * math.sin(a),
                     24.0 + 17.0 * math.cos(a), 24.0 + 17.0 * math.sin(a)), 4.4)
    c.fill(disc(24.0, 24.0, 3.2))


# --------------------------------------------------------------------------
# navigation / action glyphs -- 24x24, 2.4 px strokes
# --------------------------------------------------------------------------

NAV = 24


def icon_back(c):
    c.polyline([(15.0, 4.5), (7.5, 12.0), (15.0, 19.5)], 2.6)
    c.stroke(seg(7.5, 12.0, 20.0, 12.0), 2.4)


def icon_forward(c):
    c.polyline([(9.0, 4.5), (16.5, 12.0), (9.0, 19.5)], 2.6)


def icon_up(c):
    c.polyline([(4.5, 15.0), (12.0, 7.5), (19.5, 15.0)], 2.6)


def icon_down(c):
    c.polyline([(4.5, 9.0), (12.0, 16.5), (19.5, 9.0)], 2.6)


def icon_plus(c):
    c.stroke(seg(12.0, 4.5, 12.0, 19.5), 2.4)
    c.stroke(seg(4.5, 12.0, 19.5, 12.0), 2.4)


def icon_minus(c):
    c.stroke(seg(4.5, 12.0, 19.5, 12.0), 2.4)


def icon_close(c):
    c.stroke(seg(6.0, 6.0, 18.0, 18.0), 2.4)
    c.stroke(seg(18.0, 6.0, 6.0, 18.0), 2.4)


def icon_save(c):
    # Arrow into a tray.  A floppy disk is the more literal metaphor, but at
    # 24 px its three nested rectangles fill two thirds of the tile and turn
    # into a solid blob -- see the coverage assertion at the bottom.
    c.stroke(seg(12.0, 3.5, 12.0, 13.5), 2.4)
    c.polyline([(7.5, 9.5), (12.0, 14.0), (16.5, 9.5)], 2.4)
    c.polyline([(4.5, 15.5), (4.5, 20.0), (19.5, 20.0), (19.5, 15.5)], 2.4)


def icon_trash(c):
    c.stroke(seg(3.0, 7.0, 21.0, 7.0), 2.2)
    c.stroke(seg(9.0, 4.0, 15.0, 4.0), 2.4)
    c.polyline([(5.5, 7.0), (6.5, 20.0), (17.5, 20.0), (18.5, 7.0)], 2.2)
    c.stroke(seg(10.0, 10.5, 10.0, 17.0), 1.8)
    c.stroke(seg(14.0, 10.5, 14.0, 17.0), 1.8)


def icon_refresh(c):
    c.stroke(arc(12.0, 12.0, 7.8, 40.0, 320.0), 2.6)
    c.polyline([(15.5, 2.6), (19.6, 6.6), (15.2, 9.4)], 2.4)   # arrow head


# --------------------------------------------------------------------------
# emit
# --------------------------------------------------------------------------

TILES = [
    ("reader", APP, icon_reader), ("photos", APP, icon_photos),
    ("notes", APP, icon_notes), ("weather", APP, icon_weather),
    ("clock", APP, icon_clock), ("calendar", APP, icon_calendar),
    ("drawing", APP, icon_drawing), ("files", APP, icon_files),
    ("settings", APP, icon_settings),
    ("back", NAV, icon_back), ("forward", NAV, icon_forward),
    ("up", NAV, icon_up), ("down", NAV, icon_down),
    ("plus", NAV, icon_plus), ("minus", NAV, icon_minus),
    ("close", NAV, icon_close), ("save", NAV, icon_save),
    ("trash", NAV, icon_trash), ("refresh", NAV, icon_refresh),
]


def ascii_preview(name, px, size):
    """Render a glyph as text.

    There is no screen on the build host and no PIL to write a PNG, so this is
    how the shapes actually get reviewed: every icon is eyeballed as ASCII
    before it is trusted on the panel.
    """
    ramp = " .:-=+*#%@"
    step = 2 if size >= 48 else 1
    out = [f"--- {name} ({size}x{size}) ---"]
    for y in range(0, size, step):
        row = []
        for x in range(0, size, step):
            acc = 0
            cnt = 0
            for dy in range(step):
                for dx in range(step):
                    yy, xx = y + dy, x + dx
                    if yy < size and xx < size:
                        acc += (px[yy * size + xx] >> 24) & 0xFF
                        cnt += 1
            v = acc / cnt / 255.0
            row.append(ramp[min(9, int(v * 10))])
        out.append("".join(row))
    return "\n".join(out)


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    assets = root / "main" / "ui" / "assets"
    assets.mkdir(parents=True, exist_ok=True)

    c_lines = [
        "/* AUTO-GENERATED by tools/gen_icons.py -- do not edit by hand. */",
        '#include "app_icons.h"',
        "",
    ]
    h_lines = [
        "/* AUTO-GENERATED by tools/gen_icons.py -- do not edit by hand. */",
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "/* White ARGB8888 glyphs; tint them with lv_obj_set_style_image_recolor(). */",
    ]

    preview = "--preview" in sys.argv
    total = 0
    for name, size, fn in TILES:
        cv = Canvas(size)
        fn(cv)
        px = cv.to_argb()
        total += len(px) * 4

        # coverage sanity: an all-empty or fully-solid glyph means the
        # geometry is wrong, and that is much cheaper to catch here.  The
        # upper bound is deliberately tight -- a line glyph that reports more
        # than 60% coverage has almost certainly been filled solid.
        nz = sum(1 for p in px if (p >> 24) & 0xFF)
        frac = nz / len(px)
        assert 0.03 < frac < 0.60, f"{name}: coverage {frac:.3f} looks wrong"
        if preview:
            print(ascii_preview(f"{name} {frac*100:.0f}%", px, size))

        c_lines.append(f"/* {size}x{size}, {nz/len(px)*100:.1f}% covered */")
        c_lines.append(f"static const uint32_t {name}_map[] = {{")
        for i in range(0, len(px), 8):
            row = ", ".join(f"0x{v:08X}" for v in px[i:i + 8])
            c_lines.append("    " + row + ",")
        c_lines.append("};")
        c_lines.append("")
        c_lines.append(f"const lv_image_dsc_t icon_{name} = {{")
        c_lines.append("    .header = {")
        c_lines.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
        c_lines.append("        .cf = LV_COLOR_FORMAT_ARGB8888,")
        c_lines.append("        .flags = 0,")
        c_lines.append(f"        .w = {size},")
        c_lines.append(f"        .h = {size},")
        c_lines.append(f"        .stride = {size} * 4,")
        c_lines.append("    },")
        c_lines.append(f"    .data_size = sizeof({name}_map),")
        c_lines.append(f"    .data = (const uint8_t *){name}_map,")
        c_lines.append("};")
        c_lines.append("")

        h_lines.append(f"extern const lv_image_dsc_t icon_{name};")

    h_lines += ["", "#ifdef __cplusplus", "}", "#endif", ""]

    (assets / "app_icons.c").write_text("\n".join(c_lines) + "\n", encoding="utf-8")
    (assets / "app_icons.h").write_text("\n".join(h_lines) + "\n", encoding="utf-8")
    print(f"generated {len(TILES)} icons, {total} bytes of pixel data")


if __name__ == "__main__":
    main()
