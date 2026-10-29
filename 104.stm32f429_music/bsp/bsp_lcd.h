/**
 ******************************************************************************
 * @file    bsp_lcd.h
 * @brief   FMC Bank1 NE1 8080 16-bit LCD driver for the STM32F429 Apollo board.
 *
 *  Ported from embedded_based_on_stm32/code/00-Drivers/drv_lcd.c (the NT35510
 *  reference the project is based on) and adapted for the F429 + the U-disk
 *  GBK font path.  Adds lcd_color_fill() which LVGL's display port calls to
 *  push one rendered rectangle to the panel.
 *
 *  The panel is wired to FMC Bank1 NE1 as a 16-bit 8080 device.  RS (D/C) is
 *  on the FSMC address line selected by LCD_BASE = 0x60000000 | 0x0007FFFE,
 *  which matches the 正点原子 Apollo TFTLCD routing.
 ******************************************************************************
 */
#ifndef BSP_LCD_H
#define BSP_LCD_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ---- Panel resolution (configurable; real panel is 800x480) --------------- */
#ifndef LCD_WIDTH
#define LCD_WIDTH 800
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT 480
#endif

/* Shared status/result type.  Also defined (guarded) in bsp_lcd_text.h so a
 * translation unit can include both headers safely. */
#ifndef GLOBAL_TYPE_T_DEFINED
#define GLOBAL_TYPE_T_DEFINED
typedef enum {
    RT_OK = 0,
    RT_FAIL,
} GLOBAL_TYPE_T;
#endif

/* 8080 16-bit FMC NOR/PSRAM window. */
#define LCD_BASE ((uint32_t)(0x60000000 | 0x0007FFFE))
#define LCD ((LCD_TypeDef *)LCD_BASE)

typedef struct
{
    volatile uint16_t LCD_REG;
    volatile uint16_t LCD_RAM;
} LCD_TypeDef;

typedef struct
{
    uint16_t lcd_id;
    uint16_t lcd_dir;
    uint16_t wramcmd;
    uint16_t setxcmd;
    uint16_t setycmd;
    uint32_t lcd_width;
    uint32_t lcd_height;
} LCD_INFO_T;

/* The geometry macros are kept because the display port sizes its buffers with
 * them; the getter itself is declared inside the extern "C" block below. */
#define LCD_Width (lcd_get_info()->lcd_width)
#define LCD_Height (lcd_get_info()->lcd_height)

/* GUI colors (RGB565) */
#define LCD_WHITE 0xFFFF
#define LCD_BLACK 0x0000
#define LCD_BLUE 0x001F
#define LCD_RED 0xF800
#define LCD_GREEN 0x07E0
#define LCD_CYAN 0x7FFF
#define LCD_YELLOW 0xFFE0
#define LCD_MAGENTA 0xF81F

/* Backlight (PB5, active high) */
#define LCD_BACKLIGHT_ON() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET)
#define LCD_BACKLIGHT_OFF() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET)

#ifdef __cplusplus
extern "C" {
#endif

GLOBAL_TYPE_T lcd_driver_init(void);
void          lcd_driver_clear(uint32_t color);
void          lcd_set_cursor(uint16_t x, uint16_t y);
void          lcd_write_ram_prepare(void);
void          lcd_fast_drawpoint(uint16_t x, uint16_t y, uint32_t color);

/* Push a rendered rectangle to the panel (LVGL flush primitive).
 * (x,y) is the top-left, (w,h) the size, pixels is w*h RGB565 words. */
void lcd_color_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *pixels);

/* Read-only snapshot of the panel geometry (lcd_width / lcd_height / lcd_dir). */
const LCD_INFO_T *lcd_get_info(void);

/* 1 once the panel is up and lcd_get_info() holds the final GRAM window. */
uint8_t lcd_driver_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LCD_H */
