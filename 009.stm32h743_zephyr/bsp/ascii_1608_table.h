/**
  ******************************************************************************
  * @file    ascii_1608_table.h
  * @brief   8x16 ASCII dot-matrix glyph table (MSB-first, row scan).
  *
  *  Index = c - 0x20, valid for c in [0x20, 0x7E].  Each glyph is 16 bytes,
  *  one byte per row, bit7 = leftmost pixel (LVGL 1bpp convention).
  ******************************************************************************
  */
#ifndef __ASCII_1608_TABLE_H
#define __ASCII_1608_TABLE_H

#include <stdint.h>

extern const uint8_t ascii_1608_msb[95 * 16];

#endif /* __ASCII_1608_TABLE_H */
