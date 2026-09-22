/**
 * @file board_config.h
 * @brief Single source of truth for the Waveshare ESP32-S3-Touch-LCD-4.3B board.
 *
 * PROVENANCE (two independently derived evidence chains, both agree):
 *   1. Waveshare schematic  ESP32-S3-Touch-LCD-4.3B-Sch.pdf, page 1:
 *      - the "WROOM-1/2 | LCD | USB | SD | RS485 | CAN | RTC | DI/DO" net table
 *        (sheet coordinates x 435..700, y 400..580)
 *      - the LCD panel connector PORT1 pin/net list
 *      - the CH422G (U11) net list
 *   2. Waveshare official wiki "Pinouts" tables
 *      https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3B
 *
 * Do not change a pin in this file without re-checking both sources.
 */

#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_NAME              "ESP32-S3-Touch-LCD-4.3B"
#define BOARD_MODULE            "ESP32-S3-WROOM-1-N16R8"
#define BOARD_FLASH_SIZE_MB     (16)
#define BOARD_PSRAM_SIZE_MB     (8)

/* ------------------------------------------------------------------------- */
/* Panel geometry -- landscape, fixed for the whole application               */
/* ------------------------------------------------------------------------- */
#define BOARD_LCD_H_RES         (800)
#define BOARD_LCD_V_RES         (480)

/* ------------------------------------------------------------------------- */
/* RGB LCD interface                                                          */
/*                                                                            */
/* The panel is 5-6-5 wired: only the *upper* bits of every colour channel    */
/* leave the SoC.  R0..R2 / G0..G1 / B0..B2 exist on the panel connector but  */
/* are not driven, therefore:                                                 */
/*      LCD R3..R7  == pixel value bits 11..15                                */
/*      LCD G2..G7  == pixel value bits  5..10                                */
/*      LCD B3..B7  == pixel value bits  0..4                                 */
/* ------------------------------------------------------------------------- */
#define BOARD_LCD_PCLK_GPIO     GPIO_NUM_7
#define BOARD_LCD_DE_GPIO       GPIO_NUM_5
#define BOARD_LCD_VSYNC_GPIO    GPIO_NUM_3    /* also an ESP32-S3 strapping pin */
#define BOARD_LCD_HSYNC_GPIO    GPIO_NUM_46   /* also an ESP32-S3 strapping pin */

#define BOARD_LCD_B3_GPIO       GPIO_NUM_14
#define BOARD_LCD_B4_GPIO       GPIO_NUM_38
#define BOARD_LCD_B5_GPIO       GPIO_NUM_18
#define BOARD_LCD_B6_GPIO       GPIO_NUM_17
#define BOARD_LCD_B7_GPIO       GPIO_NUM_10
#define BOARD_LCD_G2_GPIO       GPIO_NUM_39
#define BOARD_LCD_G3_GPIO       GPIO_NUM_0    /* also the IO0 strapping / boot pin */
#define BOARD_LCD_G4_GPIO       GPIO_NUM_45
#define BOARD_LCD_G5_GPIO       GPIO_NUM_48
#define BOARD_LCD_G6_GPIO       GPIO_NUM_47
#define BOARD_LCD_G7_GPIO       GPIO_NUM_21
#define BOARD_LCD_R3_GPIO       GPIO_NUM_1
#define BOARD_LCD_R4_GPIO       GPIO_NUM_2
#define BOARD_LCD_R5_GPIO       GPIO_NUM_42
#define BOARD_LCD_R6_GPIO       GPIO_NUM_41
#define BOARD_LCD_R7_GPIO       GPIO_NUM_40

/* ------------------------------------------------------------------------- */
/* RGB timing                                                                 */
/*                                                                            */
/* Source: the DPI/RGB timing set that is confirmed working on this exact      */
/* board model.  They are blanking intervals, i.e. a wrong value shows up as   */
/* a shifted image, not as a dead panel:                                       */
/*   image shifted horizontally / diagonal bands  -> HSYNC_* , PCLK polarity   */
/*   image shifted vertically   / partial frame   -> VSYNC_*                   */
/*   no image at all                              -> lower PCLK                */
/* Increase/decrease in small steps and re-flash; keep the panel within its    */
/* data-sheet limits (max PCLK ~= 30 MHz).                                     */
/* ------------------------------------------------------------------------- */
#define BOARD_LCD_PCLK_HZ           (16 * 1000 * 1000)
#define BOARD_LCD_HSYNC_PULSE_WIDTH (8)
#define BOARD_LCD_HSYNC_BACK_PORCH  (16)
#define BOARD_LCD_HSYNC_FRONT_PORCH (16)
#define BOARD_LCD_VSYNC_PULSE_WIDTH (8)
#define BOARD_LCD_VSYNC_BACK_PORCH  (16)
#define BOARD_LCD_VSYNC_FRONT_PORCH (16)

/* Number of panel frame buffers living in PSRAM (double buffering, needed
 * for tear-free LVGL direct rendering).                                     */
#define BOARD_LCD_NUM_FB            (2)

/* ------------------------------------------------------------------------- */
/* Shared I2C bus  (CH422G expander + GT911 touch + PCF85063 RTC)             */
/* ------------------------------------------------------------------------- */
#define BOARD_I2C_PORT              (0)
#define BOARD_I2C_SDA_GPIO          GPIO_NUM_8
#define BOARD_I2C_SCL_GPIO          GPIO_NUM_9
#define BOARD_I2C_FREQ_HZ           (400 * 1000)

#define BOARD_CH422G_I2C_ADDR       (0x24)   /* mode / system parameter register */
#define BOARD_GT911_I2C_ADDR        (0x5D)
#define BOARD_GT911_I2C_ADDR_ALT    (0x14)
#define BOARD_PCF85063_I2C_ADDR     (0x51)

/* ------------------------------------------------------------------------- */
/* Capacitive touch (GT911)                                                   */
/*   RST is NOT a GPIO: it is driven by CH422G EXIO1.                         */
/* ------------------------------------------------------------------------- */
#define BOARD_TOUCH_IRQ_GPIO        GPIO_NUM_4

/* ------------------------------------------------------------------------- */
/* microSD (SPI mode; CS is NOT a GPIO: it is CH422G EXIO4, active low)       */
/* ------------------------------------------------------------------------- */
#define BOARD_SD_MOSI_GPIO          GPIO_NUM_11
#define BOARD_SD_SCK_GPIO           GPIO_NUM_12
#define BOARD_SD_MISO_GPIO          GPIO_NUM_13
#define BOARD_SD_SPI_HOST           SPI2_HOST

/* ------------------------------------------------------------------------- */
/* Other on-board peripherals (not used by the Ebook application yet)          */
/* ------------------------------------------------------------------------- */
#define BOARD_RTC_INT_GPIO          GPIO_NUM_6
#define BOARD_USB_DN_GPIO           GPIO_NUM_19
#define BOARD_USB_DP_GPIO           GPIO_NUM_20
#define BOARD_RS485_RXD_GPIO        GPIO_NUM_43
#define BOARD_RS485_TXD_GPIO        GPIO_NUM_44
#define BOARD_CAN_TX_GPIO           GPIO_NUM_15
#define BOARD_CAN_RX_GPIO           GPIO_NUM_16

/* ------------------------------------------------------------------------- */
/* CH422G I2C expander -- logical pin names                                   */
/*                                                                            */
/* CH422G has no register file: the command travels in the I2C address phase. */
/*   0x24 -> set system parameter   (byte2: [SLEEP]00[OD_EN]0[A_SCAN]0[IO_OE])*/
/*   0x38 -> write bidirectional IO (byte2: IO7..IO0)                         */
/*   0x46 -> write OC3..OC0 open-drain/general purpose outputs                */
/*   0x26 -> read  bidirectional IO                                           */
/* After reset every parameter is 0.  0x01 = IO0..IO7 outputs, OD_EN = 0.     */
/* ------------------------------------------------------------------------- */
typedef enum {
    BOARD_EXIO_DI0     = 0,   /* EXIO0 -> isolated digital input 0            */
    BOARD_EXIO_CTP_RST = 1,   /* EXIO1 -> GT911 reset, active low             */
    BOARD_EXIO_DISP    = 2,   /* EXIO2 -> LCD backlight enable, high = on     */
    BOARD_EXIO_LCD_RST = 3,   /* EXIO3 -> LCD reset, active low               */
    BOARD_EXIO_SD_CS   = 4,   /* EXIO4 -> microSD chip select, active low     */
    BOARD_EXIO_DI1     = 5,   /* EXIO5 -> isolated digital input 1            */
    BOARD_EXIO_COUNT   = 6,
} board_exio_t;

typedef enum {
    BOARD_OD_DO0 = 0,         /* OC0 -> isolated digital output 0, low = on   */
    BOARD_OD_DO1 = 1,         /* OC1 -> isolated digital output 1, low = on   */
} board_od_t;

#ifdef __cplusplus
}
#endif
