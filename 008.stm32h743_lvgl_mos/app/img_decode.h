/**
  ******************************************************************************
  * @file    img_decode.h
  * @brief   Minimal still-image decoder for the 240x240 panel: BMP + JPEG.
  *
  *  LVGL ships decoders for BMP/PNG/JPEG, but they are all switched off in
  *  lv_conf.h and they reach the file through LV_USE_FS_FATFS, which is off as
  *  well.  Turning that chain on drags the whole lv_img_decoder / lv_fs layer
  *  into a build that only ever wants to paint one full-screen picture, so the
  *  decode is done here instead, straight off FatFs into an RGB565 frame.
  *
  *  Formats
  *  -------
  *    BMP   24- or 32-bit, uncompressed (BI_RGB).  Both row orders.  Scaled
  *          down with nearest-neighbour sampling; smaller images are shown 1:1
  *          and centred rather than blown up into mush.
  *    JPEG  baseline, via ChaN's TJpgDec (third_party/tjpgd), configured for
  *          direct RGB565 output.  TJpgDec only descales by 1/2, 1/4 and 1/8,
  *          so an image that is still oversized at 1/8 is centre-cropped.
  *
  *  The frame is decoded in full before anything is put on the panel: a file
  *  that turns out to be broken half way through must leave the browser on
  *  screen intact, not a half-painted picture.
  ******************************************************************************
  */
#ifndef IMG_DECODE_H
#define IMG_DECODE_H

#include <stdint.h>

#define IMG_W   240
#define IMG_H   240

typedef struct
{
    int         src_w;      /**< pixels in the file                         */
    int         src_h;
    int         out_w;      /**< pixels actually drawn (after fit/crop)     */
    int         out_h;
    int         scale_num;  /**< out_w * 100 / src_w, for the info line     */
    const char *format;     /**< "BMP" / "JPEG"                             */
    uint32_t    bytes;      /**< file size                                  */
} img_info_t;

/** Decode `path` into the internal frame, letterboxed and centred on black.
 *  Returns 0 on success.  On failure *reason points at a short UTF-8 string
 *  suitable for the on-screen error card, and the frame is untouched. */
int             img_decode_file(const char *path, img_info_t *info,
                                const char **reason);

/** The decoded frame, IMG_W * IMG_H RGB565.  Valid after a successful decode. */
uint16_t       *img_framebuffer(void);

/** Push the decoded frame to the panel (the caller must own the display). */
void            img_blit(void);

#endif /* IMG_DECODE_H */
