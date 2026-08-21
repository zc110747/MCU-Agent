/**
  ******************************************************************************
  * @file    ascii_2412_table.h
  * @brief   12x24 ASCII dot-matrix glyph table (MSB-first, row scan).
  *
  *  Index = c - 0x20, valid for c in [0x20, 0x7E].  Each glyph is 48 bytes,
  *  2 bytes per row, 24 rows.  bit7 = leftmost pixel (LVGL 1bpp convention).
  ******************************************************************************
  */
#ifndef __ASCII_2412_TABLE_H
#define __ASCII_2412_TABLE_H

#include <stdint.h>

extern const uint8_t ascii_2412_msb[95 * 48];

#endif /* __ASCII_2412_TABLE_H */
