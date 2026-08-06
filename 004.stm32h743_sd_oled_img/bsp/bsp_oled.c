/**
  ******************************************************************************
  * @file    bsp_oled.c
  * @brief   Application-facing OLED helpers.
  ******************************************************************************
  */

#include "bsp_oled.h"
#include "drv_oled_fonts.h"

#include <string.h>

GlobalType_t bsp_oled_init(void)
{
    GlobalType_t ret;

    ret = driver_spi_oled_init();

    LCD_SetDirection(Direction_V);
    LCD_SetBackColor(LCD_BLACK);
    LCD_SetColor(LCD_WHITE);
    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_Clear();

    return ret;
}

void bsp_oled_clear(uint32_t rgb888)
{
    LCD_SetBackColor(rgb888);
    LCD_Clear();
}

void bsp_oled_blit_frame(const uint16_t *frame)
{
    if (frame == NULL) {
        return;
    }
    LCD_CopyBuffer(0, 0, OLED_WIDTH, OLED_HEIGHT, (uint16_t *)frame);
}

void bsp_oled_blit_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels)
{
    if (pixels == NULL || w == 0U || h == 0U) {
        return;
    }
    LCD_CopyBuffer(x, y, w, h, (uint16_t *)pixels);
}

void bsp_oled_show_text(uint16_t x, uint16_t y, const char *text)
{
    char padded[41];
    size_t len;

    if (text == NULL) {
        return;
    }

    /* ASCII_Font16 is 8px wide -> 30 characters fill a 240px line */
    len = strlen(text);
    if (len > 30U) {
        len = 30U;
    }
    memcpy(padded, text, len);
    memset(padded + len, ' ', 30U - len);
    padded[30] = '\0';

    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_DisplayText(x, y, padded);
}

void bsp_oled_show_banner(const char *line1, const char *line2)
{
    LCD_SetBackColor(LCD_BLACK);
    LCD_SetColor(LCD_WHITE);
    LCD_Clear();

    if (line1 != NULL) {
        LCD_SetAsciiFont(&ASCII_Font24);
        /* 12px per char, centre horizontally as best we can */
        uint16_t w1 = (uint16_t)(strlen(line1) * 12U);
        uint16_t x1 = (w1 >= OLED_WIDTH) ? 0U : (uint16_t)((OLED_WIDTH - w1) / 2U);
        LCD_DisplayText(x1, 100, (char *)line1);
    }

    if (line2 != NULL) {
        LCD_SetAsciiFont(&ASCII_Font16);
        uint16_t w2 = (uint16_t)(strlen(line2) * 8U);
        uint16_t x2 = (w2 >= OLED_WIDTH) ? 0U : (uint16_t)((OLED_WIDTH - w2) / 2U);
        LCD_DisplayText(x2, 136, (char *)line2);
    }
}
