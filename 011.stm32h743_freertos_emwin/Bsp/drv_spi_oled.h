/**
  ******************************************************************************
  * @file    drv_spi_oled.h
  * @brief   240x240 ST7789 panel driver over SPI6 (half duplex, TX only).
  *
  *   SCK  = PG13   MOSI = PG14   NSS = PG8
  *   DC   = PG15   BL   = PG12
  ******************************************************************************
  */
#ifndef __SPI_OLED_H
#define __SPI_OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "drv_oled_fonts.h"

/* Panel geometry ------------------------------------------------------------*/
#define LCD_Width               240
#define LCD_Height              240

/* Display orientation -------------------------------------------------------*/
#define Direction_H             0   /* landscape                    */
#define Direction_H_Flip        1   /* landscape, rotated 180 deg   */
#define Direction_V             2   /* portrait                     */
#define Direction_V_Flip        3   /* portrait, rotated 180 deg    */

/* 24 bit RGB colours (converted to RGB565 internally) -----------------------*/
#define LCD_WHITE       0xFFFFFF
#define LCD_BLACK       0x000000
#define LCD_BLUE        0x0000FF
#define LCD_GREEN       0x00FF00
#define LCD_RED         0xFF0000
#define LCD_CYAN        0x00FFFF
#define LCD_MAGENTA     0xFF00FF
#define LCD_YELLOW      0xFFFF00
#define LCD_GREY        0x2C2C2C

#define LIGHT_BLUE      0x8080FF
#define LIGHT_GREEN     0x80FF80
#define LIGHT_RED       0xFF8080
#define LIGHT_CYAN      0x80FFFF
#define LIGHT_MAGENTA   0xFF80FF
#define LIGHT_YELLOW    0xFFFF80
#define LIGHT_GREY      0xA3A3A3

#define DARK_BLUE       0x000080
#define DARK_GREEN      0x008000
#define DARK_RED        0x800000
#define DARK_CYAN       0x008080
#define DARK_MAGENTA    0x800080
#define DARK_YELLOW     0x808000
#define DARK_GREY       0x404040

/* Padding mode for numeric output */
#define Fill_Zero       0
#define Fill_Space      1

/**
  * @brief  Runtime state of the panel.
  */
typedef struct
{
    uint32_t Color;         /* pen colour, RGB565      */
    uint32_t BackColor;     /* background, RGB565      */
    uint8_t  ShowNum_Mode;  /* Fill_Zero / Fill_Space  */
    uint8_t  Direction;     /* Direction_*             */
    uint16_t Width;         /* visible width           */
    uint16_t Height;        /* visible height          */
    uint8_t  X_Offset;      /* controller GRAM offset  */
    uint8_t  Y_Offset;
} OLED_INFO;

/* Initialisation ------------------------------------------------------------*/
GlobalType_t driver_spi_oled_init(void);

/* Configuration -------------------------------------------------------------*/
void OLED_SetDirection(uint8_t direction);
void OLED_SetBackColor(uint32_t Color);
void OLED_SetColor(uint32_t Color);
void OLED_SetAsciiFont(pFONT *Asciifonts);
void OLED_SetTextFont(pFONT *fonts);
void OLED_ShowNumMode(uint8_t mode);

/* Drawing -------------------------------------------------------------------*/
void OLED_Clear(void);
void OLED_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t Color);
void OLED_CopyBuffer(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *DataBuff);

/* Text ----------------------------------------------------------------------*/
void OLED_DisplayText(uint16_t x, uint16_t y, char *pText);
void OLED_DisplayNumber(uint16_t x, uint16_t y, int32_t number, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* __SPI_OLED_H */
