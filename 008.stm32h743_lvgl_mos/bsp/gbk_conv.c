/*
 * bsp/gbk_conv.c
 * Minimal GBK(CP936) -> UTF-8 transcoder for the serial console.
 * See gbk_conv.h for the rationale.
 */

#include "gbk_conv.h"
#include "gbk_unicode_tbl.h"

#include <stdint.h>

#define GBK_TRAIL_A_MIN  0x40
#define GBK_TRAIL_A_MAX  0x7E
#define GBK_TRAIL_B_MIN  0x80
#define GBK_TRAIL_B_MAX  0xFE

/* trail 0x40..0x7E -> 0..62 ; trail 0x80..0xFE -> 63..189 */
static uint16_t gbk_lookup(uint8_t lead, uint8_t trail)
{
    int off;

    if (trail >= GBK_TRAIL_A_MIN && trail <= GBK_TRAIL_A_MAX)
    {
        off = (int)trail - GBK_TRAIL_A_MIN;
    }
    else if (trail >= GBK_TRAIL_B_MIN && trail <= GBK_TRAIL_B_MAX)
    {
        off = 63 + ((int)trail - GBK_TRAIL_B_MIN);
    }
    else
    {
        return 0U;
    }

    if (lead < GBK_LEAD_MIN || lead > GBK_LEAD_MAX)
    {
        return 0U;
    }

    {
        uint32_t idx = (uint32_t)(lead - GBK_LEAD_MIN) * (uint32_t)GBK_COLS + (uint32_t)off;
        return gbk_to_unicode[idx];
    }
}

int gbk_to_utf8(const char *gbk, char *out, int out_size)
{
    const uint8_t *p;
    int            o;

    if (gbk == NULL || out == NULL || out_size <= 0)
    {
        return 0;
    }

    p = (const uint8_t *)gbk;
    o = 0;

    while (*p != '\0')
    {
        uint8_t b = *p;

        if (b < 0x80U)
        {
            /* ASCII pass-through */
            if (o + 1 >= out_size)
            {
                break;
            }
            out[o++] = (char)b;
            p++;
        }
        else
        {
            uint8_t  lead = b;
            uint8_t  trail = (p[1] != '\0') ? p[1] : (uint8_t)0U;
            uint16_t uc = (p[1] != '\0') ? gbk_lookup(lead, trail) : (uint16_t)0U;

            if (uc == 0U)
            {
                /* unmappable -> '?' */
                if (o + 1 >= out_size)
                {
                    break;
                }
                out[o++] = '?';
                p += (p[1] != '\0') ? 2 : 1;
            }
            else if (uc <= 0x7FFU)
            {
                if (o + 2 >= out_size)
                {
                    break;
                }
                out[o++] = (char)(0xC0U | (uc >> 6));
                out[o++] = (char)(0x80U | (uc & 0x3FU));
                p += 2;
            }
            else
            {
                if (o + 3 >= out_size)
                {
                    break;
                }
                out[o++] = (char)(0xE0U | (uc >> 12));
                out[o++] = (char)(0x80U | ((uc >> 6) & 0x3FU));
                out[o++] = (char)(0x80U | (uc & 0x3FU));
                p += 2;
            }
        }
    }

    out[o] = '\0';
    return o;
}

int utf8_is_valid(const uint8_t *buf, uint32_t len)
{
    uint32_t i = 0U;

    if (buf == NULL)
    {
        return 0;
    }

    while (i < len)
    {
        uint8_t  b    = buf[i];
        uint32_t need;        /* number of continuation bytes expected */
        uint32_t min_cp;      /* minimum code point (overlong guard)  */
        uint32_t cp   = 0U;
        uint32_t k;

        if (b < 0x80U)
        {
            i++;
            continue;
        }
        else if ((b & 0xE0U) == 0xC0U)      /* 110xxxxx : 2-byte */
        {
            if (b < 0xC2U)                  /* 0xC0/0xC1 are overlong */
            {
                return 0;
            }
            need   = 1U;
            min_cp = 0x80U;
        }
        else if ((b & 0xF0U) == 0xE0U)      /* 1110xxxx : 3-byte */
        {
            need   = 2U;
            min_cp = 0x800U;
        }
        else if ((b & 0xF8U) == 0xF0U)      /* 11110xxx : 4-byte */
        {
            if (b > 0xF4U)                  /* > U+10FFFF is invalid */
            {
                return 0;
            }
            need   = 3U;
            min_cp = 0x10000U;
        }
        else                                 /* 0x80..0xBF lone, or 0xF8..0xFF */
        {
            return 0;
        }

        /* Not enough bytes left for the full sequence.  Tolerate only when it
         * is the very tail of the buffer (a truncated read); there is nothing
         * more to validate past it. */
        if (i + need >= len)
        {
            return 1;
        }

        cp = (uint32_t)(b & ((0x7FU >> need) & 0x7FU));
        for (k = 1U; k <= need; k++)
        {
            uint8_t c = buf[i + k];

            if ((c & 0xC0U) != 0x80U)       /* not a continuation byte */
            {
                return 0;
            }
            cp = (cp << 6) | (uint32_t)(c & 0x3FU);
        }

        if (cp < min_cp)                     /* overlong encoding */
        {
            return 0;
        }
        if ((cp >= 0xD800U) && (cp <= 0xDFFFU)) /* surrogate halves */
        {
            return 0;
        }

        i += need + 1U;
    }

    return 1;
}
