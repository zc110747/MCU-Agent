# -*- coding: utf-8 -*-
"""Generate Bsp/lv_gbk_map.c - a Unicode(BMP) -> GBK lookup table.

Why a generated table instead of FatFs' ff_uni2oem()?
    ffunicode.c does contain a CP936 table, but enabling FF_CODE_PAGE 936 pulls
    in *both* directions (uni2oem936 + oem2uni936, ~170 KB of flash) and would
    also change FatFs' short-file-name behaviour half way through the project.
    We only ever need one direction, so a purpose-built table is 4x smaller.

Layout produced here:
    1. 0x4E00..0x9FA5 (CJK Unified Ideographs) - every single one of the 20902
       code points has a GBK counterpart, so a dense uint16_t array indexed by
       (unicode - 0x4E00) is both the smallest and the fastest representation.
    2. Everything else (889 entries: Latin-1/Greek/Cyrillic accents, general
       punctuation, CJK punctuation + kana + bopomofo, full-width forms) is
       stored as a sorted uint32_t array packed as (unicode << 16 | gbk) and
       resolved with a binary search.

    41804 + 3556 = 45360 bytes of .rodata.
"""
import io
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'Bsp', 'lv_gbk_map.c')

CJK_LO, CJK_HI = 0x4E00, 0x9FA5

dense = []
misc = []
for u in range(0x80, 0x10000):
    if 0xD800 <= u <= 0xDFFF:          # surrogates are not real characters
        continue
    try:
        b = chr(u).encode('gbk')
    except UnicodeEncodeError:
        b = None
    gbk = (b[0] << 8) | b[1] if (b and len(b) == 2) else 0
    if CJK_LO <= u <= CJK_HI:
        dense.append(gbk)
    elif gbk:
        misc.append((u, gbk))

assert len(dense) == CJK_HI - CJK_LO + 1
assert all(dense), 'expected the whole CJK block to be covered by GBK'
misc.sort()


def emit_rows(values, fmt, per_line):
    out = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        out.append('    ' + ' '.join(fmt % v + ',' for v in chunk))
    return '\n'.join(out)


body = u'''/**
  ******************************************************************************
  * @file    lv_gbk_map.c
  * @brief   Unicode (BMP) -> GBK code conversion table.
  *
  *  GENERATED FILE - do not edit by hand, run scripts/gen_gbk_map.py instead.
  *
  *  LVGL hands us UTF-8 decoded Unicode code points, while the GBKxx.FON glyph
  *  files on the SD card are indexed by the 2-byte GBK code.  This table is the
  *  bridge between the two.
  *
  *  Storage strategy
  *  ----------------
  *   - 0x4E00..0x9FA5 (CJK Unified Ideographs): all %(dense_n)d code points exist in
  *     GBK, so a dense array indexed by (unicode - 0x4E00) is used.  O(1).
  *   - the remaining %(misc_n)d mappings (punctuation, full-width forms, kana,
  *     Greek/Cyrillic, ...) live in a sorted (unicode<<16 | gbk) array that is
  *     resolved with a binary search.  O(log n).
  *
  *  Total .rodata cost: %(bytes)d bytes.
  ******************************************************************************
  */
#include "lv_gbk_map.h"

#define GBK_CJK_LO   0x4E00u
#define GBK_CJK_HI   0x9FA5u

/* unicode - 0x4E00 -> GBK code */
static const uint16_t gbk_cjk_tbl[%(dense_n)d] = {
%(dense_body)s
};

/* (unicode << 16) | gbk, sorted by unicode */
static const uint32_t gbk_misc_tbl[%(misc_n)d] = {
%(misc_body)s
};

/**
  * @brief  Translate a Unicode code point to its GBK code.
  * @param  unicode  Unicode code point (BMP).
  * @retval GBK code (high byte first, e.g. 0xD6D0 for U+4E2D), or 0 when the
  *         character has no GBK representation.  ASCII (< 0x80) also returns 0
  *         because those glyphs come from the compiled-in ASCII tables, not
  *         from the GBK font files.
  */
uint16_t lv_gbk_from_unicode(uint32_t unicode)
{
    uint32_t lo, hi, mid;

    if (unicode < 0x80u || unicode > 0xFFFFu)
    {
        return 0u;
    }

    if (unicode >= GBK_CJK_LO && unicode <= GBK_CJK_HI)
    {
        return gbk_cjk_tbl[unicode - GBK_CJK_LO];
    }

    lo = 0u;
    hi = (uint32_t)(sizeof(gbk_misc_tbl) / sizeof(gbk_misc_tbl[0]));
    while (lo < hi)
    {
        uint32_t key;

        mid = lo + ((hi - lo) >> 1);
        key = gbk_misc_tbl[mid] >> 16;

        if (key == unicode)
        {
            return (uint16_t)(gbk_misc_tbl[mid] & 0xFFFFu);
        }
        else if (key < unicode)
        {
            lo = mid + 1u;
        }
        else
        {
            hi = mid;
        }
    }

    return 0u;
}
''' % {
    'dense_n': len(dense),
    'misc_n': len(misc),
    'bytes': len(dense) * 2 + len(misc) * 4,
    'dense_body': emit_rows(dense, '0x%04X', 12),
    'misc_body': emit_rows([(u << 16) | g for u, g in misc], '0x%08X', 8),
}

io.open(OUT, 'w', encoding='utf-8', newline='\n').write(body)
print('written: %s' % os.path.normpath(OUT))
print('  dense CJK entries : %d (%d bytes)' % (len(dense), len(dense) * 2))
print('  misc  entries     : %d (%d bytes)' % (len(misc), len(misc) * 4))
print('  total .rodata     : %d bytes' % (len(dense) * 2 + len(misc) * 4))
# a few spot checks so a broken codec shows up immediately
for ch in u'\u4e2d\u6587\u65f6\u949f\u5361\uff1a\u3001':
    u = ord(ch)
    g = dense[u - CJK_LO] if CJK_LO <= u <= CJK_HI else dict(misc)[u]
    print('  U+%04X -> GBK 0x%04X' % (u, g))
