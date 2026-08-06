/**
  ******************************************************************************
  * @file    app_image.h
  * @brief   JPEG -> 240x240 RGB565 pipeline (crop first, then scale).
  ******************************************************************************
  */

#ifndef __APP_IMAGE_H
#define __APP_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "bsp_oled.h"

/** Statistics reported back after a successful decode. */
typedef struct
{
    uint16_t src_width;     /*!< width of the JPEG as stored on the card      */
    uint16_t src_height;    /*!< height of the JPEG as stored on the card     */
    uint16_t out_width;     /*!< width after the TJpgDec hardware descale     */
    uint16_t out_height;    /*!< height after the TJpgDec hardware descale    */
    uint16_t crop_side;     /*!< side of the centred square that was kept     */
    uint8_t  scale;         /*!< descale exponent used (0..3 => 1/1 .. 1/8)   */
    int      jres;          /*!< raw JRESULT from TJpgDec (0 == JDR_OK)       */
    uint32_t elapsed_ms;    /*!< wall clock time of the whole decode          */
} app_image_info_t;

/**
  * @brief  The single 240x240 RGB565 frame buffer shared by the whole app.
  * @note   112.5 KB, lives in AXI SRAM (RAM_D1).
  */
uint16_t *app_image_framebuffer(void);

/**
  * @brief  Decode one JPEG file straight into the frame buffer.
  *
  * Pipeline:
  *   1. jd_prepare() reads the headers and gives us the source geometry.
  *   2. The largest 1/1, 1/2, 1/4 or 1/8 descale that still covers 240x240 is
  *      picked, so the decoder itself does most of the shrinking for free.
  *   3. A centred square is cropped out of the descaled image.
  *   4. Every output MCU block is resampled into the frame buffer with a
  *      reverse (gather) mapping, which cannot leave holes the way a forward
  *      nearest-neighbour scatter would.
  *
  * @param  path  full FatFs path, e.g. "0:/image/cat.jpg"
  * @param  info  optional, may be NULL
  * @retval RT_OK on success
  */
GlobalType_t app_image_decode_file(const char *path, app_image_info_t *info);

/** Human readable form of the last TJpgDec result code. */
const char *app_image_jres_str(int jres);

#ifdef __cplusplus
}
#endif

#endif /* __APP_IMAGE_H */
