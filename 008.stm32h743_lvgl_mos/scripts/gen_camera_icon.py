#!/usr/bin/env python3
"""Generate a 48x48 RGB565 camera icon as a C uint8_t array (little-endian
pixels, matching the existing menu_icons.c convention)."""

W = H = 48

def rgb565(r, g, b):
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    v = (r5 << 11) | (g6 << 5) | b5
    return bytes([v & 0xFF, (v >> 8) & 0xFF])

WHITE = (255, 255, 255)
BODY  = (96, 116, 140)     # camera body, slate blue-grey
BODY2 = (74, 92, 114)      # top bump, slightly darker
RING  = (38, 44, 54)       # lens ring
LENS  = (16, 18, 22)       # lens glass
HILITE= (220, 232, 250)    # lens glint + flash
FLASH = (250, 220, 120)    # flash dot

def in_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    # corner rounding
    cx = None; cy = None
    if x < x0 + r and y < y0 + r:
        cx, cy = x0 + r, y0 + r
    elif x > x1 - r and y < y0 + r:
        cx, cy = x1 - r, y0 + r
    elif x < x0 + r and y > y1 - r:
        cx, cy = x0 + r, y1 - r
    elif x > x1 - r and y > y1 - r:
        cx, cy = x1 - r, y1 - r
    if cx is None:
        return True
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r

def dist2(x, y, cx, cy):
    return (x - cx) ** 2 + (y - cy) ** 2

pixels = []
for y in range(H):
    for x in range(W):
        col = WHITE
        # top viewfinder bump
        if in_round_rect(x, y, 17, 6, 31, 15, 3):
            col = BODY2
        # body
        if in_round_rect(x, y, 5, 13, 43, 39, 8):
            col = BODY
        # lens ring
        if dist2(x, y, 24, 26) <= 9 * 9 and dist2(x, y, 24, 26) >= 6 * 6:
            col = RING
        # lens glass
        if dist2(x, y, 24, 26) < 6 * 6:
            col = LENS
        # lens glint
        if dist2(x, y, 21, 23) <= 2 * 2:
            col = HILITE
        # flash dot
        if dist2(x, y, 38, 18) <= 2 * 2:
            col = FLASH
        pixels.append(rgb565(*col))

# Emit C array
out = []
out.append("static const uint8_t g_icon_camera[4608] =")
out.append("{")
per = 12
for i in range(0, len(pixels), per):
    chunk = pixels[i:i + per]
    line = "    " + ", ".join("0x%02X, 0x%02X" % (b0, b1) for b0, b1 in chunk) + ","
    out.append(line)
out.append("};")
out.append("")
out.append("const lv_img_dsc_t icon_camera =")
out.append("{")
out.append("    .header.cf       = LV_IMG_CF_TRUE_COLOR,")
out.append("    .header.w        = 48,")
out.append("    .header.h        = 48,")
out.append("    .header.reserved = 0,")
out.append("    .data_size       = sizeof(g_icon_camera),")
out.append("    .data            = g_icon_camera")
out.append("};")

import sys
sys.stdout.write("\n".join(out) + "\n")
