/**
  ******************************************************************************
  * @file    lv_gbk_map.h
  * @brief   Unicode (BMP) -> GBK code conversion.
  ******************************************************************************
  */
#ifndef __LV_GBK_MAP_H
#define __LV_GBK_MAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Translate a Unicode code point to its GBK code.
  * @param  unicode  Unicode code point (BMP, i.e. <= 0xFFFF).
  * @retval GBK code with the lead byte in the high half (U+4E2D -> 0xD6D0),
  *         or 0 when the code point has no GBK representation.
  *         ASCII (< 0x80) deliberately returns 0: those glyphs are served from
  *         the compiled-in ASCII tables, not from the SD card font files.
  */
uint16_t lv_gbk_from_unicode(uint32_t unicode);

#ifdef __cplusplus
}
#endif

#endif /* __LV_GBK_MAP_H */
