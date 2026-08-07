/**
 * @file    main.h
 * @brief   Board level definitions for the STM32H743ZIT6 (LXB743ZI-P1) face
 *          detection demo: DCMI/OV5640 -> CMSIS-NN face detector -> ST7789.
 *
 * Pin map (verified against LXB743ZI-P1 schematic):
 *   DCMI  : PC6 D0, PC7 D1, PG10 D2, PG11 D3, PE4 D4, PD3 D5, PE5 D6, PE6 D7
 *           PA4 HSYNC, PA6 PIXCLK, PG9 VSYNC
 *   SCCB  : I2C4  PF14 SCL, PF15 SDA   (OV5640 @ 0x78)
 *   OLED  : SPI6  PG8 NSS, PG13 SCK, PG14 MOSI, PG15 DC, PG12 BL (ST7789)
 *   UART  : USART1 PA9 TX, PA10 RX     (debug console, 115200 8N1)
 *   Misc  : PF13 CAMERA_PWDN, PG7 LED
 */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    RT_OK = 0,
    RT_FAIL,
} GlobalType_t;

#ifndef UINT_MAX
#define UINT_MAX 0xFFFFFFFF
#endif

/* ------------------------------------------------------------------ pins */
#define DCMI_PWDN_Pin           GPIO_PIN_13
#define DCMI_PWDN_GPIO_Port     GPIOF
#define SCCB_SCL_Pin            GPIO_PIN_14
#define SCCB_SCL_GPIO_Port      GPIOF
#define SCCB_SDA_Pin            GPIO_PIN_15
#define SCCB_SDA_GPIO_Port      GPIOF
#define LED_Pin                 GPIO_PIN_7
#define LED_GPIO_Port           GPIOG
#define LCD_BL_Pin              GPIO_PIN_12
#define LCD_BL_GPIO_Port        GPIOG
#define LCD_DC_Pin              GPIO_PIN_15
#define LCD_DC_GPIO_Port        GPIOG

/* USART1 debug console on the SWD/USART1 header. */
#define DBG_UART_TX_Pin         GPIO_PIN_9
#define DBG_UART_RX_Pin         GPIO_PIN_10
#define DBG_UART_GPIO_Port      GPIOA

#define LED_ON()                HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET)
#define LED_OFF()               HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET)
#define LED_TOGGLE()            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin)

#define LCD_DC_DATA()           HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET)
#define LCD_DC_COMMAND()        HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET)

#define LCD_BL_OFF()            HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET)
#define LCD_BL_ON()             HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET)

#define DCMI_PWDN_OFF()         HAL_GPIO_WritePin(DCMI_PWDN_GPIO_Port, DCMI_PWDN_Pin, GPIO_PIN_RESET)
#define DCMI_PWDN_ON()          HAL_GPIO_WritePin(DCMI_PWDN_GPIO_Port, DCMI_PWDN_Pin, GPIO_PIN_SET)

/* --------------------------------------------------------------- sections */
/* Non-cacheable AXI SRAM at 0x24000000 - DCMI DMA target. */
#define DMA_BUFFER              __attribute__((section(".dma_buffer"), aligned(32)))
/* Cacheable AXI SRAM at 0x24040000 - CPU-only scratch (NN arena, LCD buffer) */
#define AXI_RAM                 __attribute__((section(".axi_ram"), aligned(32)))

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
