/**
  ******************************************************************************
  * @file    bsp_oled.h
  * @brief   Thin application-facing layer on top of the ST7789 SPI driver.
  *
  * The low level driver (drv_spi_oled.c) was supplied pre-debugged and is kept
  * unchanged apart from a NULL guard; everything the slideshow needs lives here.
  ******************************************************************************
  */

#ifndef __BSP_OLED_H
#define __BSP_OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "drv_spi_oled.h"

#define OLED_WIDTH      LCD_WIDTH       /* 240 */
#define OLED_HEIGHT     LCD_HEIGHT      /* 240 */
#define OLED_PIXELS     (OLED_WIDTH * OLED_HEIGHT)

/** Bring up the panel (SPI already initialised by main) and clear it. */
GlobalType_t bsp_oled_init(void);

/** Fill the whole panel with a 0xRRGGBB colour. */
void bsp_oled_clear(uint32_t rgb888);

/**
  * @brief  Push a full 240x240 RGB565 frame to the panel.
  * @param  frame  pointer to OLED_PIXELS uint16_t values, 4-byte aligned.
  */
void bsp_oled_blit_frame(const uint16_t *frame);

/** Push an arbitrary rectangle of RGB565 pixels. */
void bsp_oled_blit_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels);

/** Centred two-line message on a black background (error / status screens). */
void bsp_oled_show_banner(const char *line1, const char *line2);

/**
  * @brief  One line of ASCII text drawn at a fixed position.
  * @note   Text is padded with spaces so leftovers from a longer previous
  *         string are erased.
  */
void bsp_oled_show_text(uint16_t x, uint16_t y, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_OLED_H */
