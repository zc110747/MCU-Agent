/**
 * @file gbk_table.h
 * @brief CP936 (GBK) -> Unicode BMP lookup table. GENERATED - DO NOT EDIT.
 *
 * Regenerate with:  python tools/gen_gbk_table.py
 * Source of truth:   the host's cp936 codec (i.e. Microsoft code page 936).
 *
 * Layout:  gbk_unicode_table[(lead - 0x81) * GBK_TRAIL_COUNT +
 *                            (trail - 0x40)]
 * 0x0000 marks a slot CP936 does not define.
 */
#pragma once

#include <stdint.h>

#define GBK_LEAD_MIN    (0x81u)
#define GBK_LEAD_MAX    (0xFEu)
#define GBK_TRAIL_MIN   (0x40u)
#define GBK_TRAIL_MAX   (0xFEu)
#define GBK_TRAIL_COUNT (191u)

/** @brief CP936 maps this byte to the euro sign rather than to a lead byte. */
#define GBK_SINGLE_EURO (0x80u)

/** @brief 24066 entries, little used slots hold 0. */
extern const uint16_t gbk_unicode_table[24066];
