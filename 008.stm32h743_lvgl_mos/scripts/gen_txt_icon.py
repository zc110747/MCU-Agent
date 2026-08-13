#!/usr/bin/env python3
# One-off generator: append a 48x48 RGB565 "document with text" icon to
# app/menu_icons.c / app/menu_icons.h, matching the existing icon format.
import os

WHITE = 0xFFFF
BLUE = 0x0319
W = H = 48


def px(x, y):
    in_x = 12 <= x <= 35
    in_y = 6 <= y <= 41
    border = False
    if in_x and in_y:
        if (12 <= x <= 13 or 34 <= x <= 35) or (6 <= y <= 7 or 40 <= y <= 41):
            border = True
    # carve out the top-right corner so it reads as a folded page
    if 34 <= x <= 41 and 6 <= y <= 13:
        border = False
    if border:
        return BLUE
    # fold crease diagonal
    if 34 <= x <= 41 and 6 <= y <= 13 and (x - 34) == (13 - y):
        return BLUE
    # text lines
    for ly in (14, 20, 26, 32):
        if ly <= y <= ly + 2 and 16 <= x <= 30:
            return BLUE
    if 38 <= y <= 39 and 16 <= x <= 24:
        return BLUE
    return WHITE


def rgb565_bytes(v):
    return [v & 0xFF, (v >> 8) & 0xFF]


lines = []
pixels = []
for y in range(H):
    for x in range(W):
        pixels.append(px(x, y))

# format 12 bytes per line (6 pixels), comma separated, matching menu_icons.c
for i in range(0, len(pixels), 6):
    chunk = pixels[i:i + 6]
    b = []
    for v in chunk:
        b += rgb565_bytes(v)
    lines.append("    " + ", ".join("0x%02X" % x for x in b) + ",")

body = "\n".join(lines)

decl = (
    "\n"
    "static const uint8_t g_icon_txt[4608] =\n"
    "{\n"
    "%s\n"
    "};\n"
    "\n"
    "const lv_img_dsc_t icon_txt =\n"
    "{\n"
    "    .header.cf       = LV_IMG_CF_TRUE_COLOR,\n"
    "    .header.w        = 48,\n"
    "    .header.h        = 48,\n"
    "    .header.reserved = 0,\n"
    "    .data_size       = sizeof(g_icon_txt),\n"
    "    .data            = g_icon_txt\n"
    "};\n"
) % body

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
c_path = os.path.join(root, "app", "menu_icons.c")
h_path = os.path.join(root, "app", "menu_icons.h")

with open(c_path, "r", encoding="utf-8") as f:
    csrc = f.read()
with open(h_path, "r", encoding="utf-8") as f:
    hsrc = f.read()

if "icon_txt" in csrc:
    print("icon_txt already present, skipping")
else:
    with open(c_path, "a", encoding="utf-8") as f:
        f.write(decl)
    # add extern to header after icon_folder
    hsrc = hsrc.replace(
        "extern const lv_img_dsc_t icon_folder;",
        "extern const lv_img_dsc_t icon_folder;\nextern const lv_img_dsc_t icon_txt;",
    )
    with open(h_path, "w", encoding="utf-8") as f:
        f.write(hsrc)
    print("appended icon_txt to menu_icons.c/.h")
