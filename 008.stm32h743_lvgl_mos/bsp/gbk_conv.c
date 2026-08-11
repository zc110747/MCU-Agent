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
