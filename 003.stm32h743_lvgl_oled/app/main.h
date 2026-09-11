/**
  ******************************************************************************
  * @file    main.h
  * @brief   Common defines / board pin map for the STM32H743ZIT6 OLED demo.
  *
  * Hardware map
  * ------------
  *   LED        : PG7
  *   Debug UART : USART1  TX = PA9   RX = PA10   (115200-8-N-1)
  *   OLED (SPI6): SCK = PG13, MOSI = PG14, NSS = PG8,
  *                DC  = PG15, BL   = PG12
  *   SD card    : SDMMC1  D0..D3 = PC8..PC11, CK = PC12, CMD = PD2
  ******************************************************************************
  */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
typedef enum
{
    RT_OK = 0,
    RT_FAIL,
} GlobalType_t;

#ifndef UINT_MAX
#define UINT_MAX 0xFFFFFFFFU
#endif

/* Exported handles ----------------------------------------------------------*/
extern SPI_HandleTypeDef  hspi6;
extern UART_HandleTypeDef huart1;
extern SD_HandleTypeDef   hsd1;

/* Clock source actually in use.
 * The board carries a 25 MHz PASSIVE crystal on OSC_IN(PH0)/OSC_OUT(PH1),
 * so HSE is started with RCC_HSE_ON (never RCC_HSE_BYPASS). */
typedef enum
{
    CLOCK_SRC_HSE_XTAL = 0,      /* normal: 25 MHz crystal -> PLL1 -> 480 MHz */
    CLOCK_SRC_HSI_FALLBACK,      /* crystal never started at boot            */
    CLOCK_SRC_HSI_CSS_RESCUE     /* crystal died at runtime, CSS took over    */
} ClockSource_t;

extern volatile ClockSource_t g_clock_source;
extern volatile uint8_t       g_hse_css_fault;

/* Exported functions --------------------------------------------------------*/
void Error_Handler(void);
void SystemClock_Config(void);

/* ---------------------------------------------------------------------------
 * Pin definitions
 * -------------------------------------------------------------------------*/
#define LED_Pin             GPIO_PIN_7
#define LED_GPIO_Port       GPIOG

#define LCD_BL_Pin          GPIO_PIN_12
#define LCD_BL_GPIO_Port    GPIOG

#define LCD_DC_Pin          GPIO_PIN_15
#define LCD_DC_GPIO_Port    GPIOG

/* SPI6 pins (configured in HAL_SPI_MspInit) */
#define LCD_NSS_Pin         GPIO_PIN_8
#define LCD_SCK_Pin         GPIO_PIN_13
#define LCD_MOSI_Pin        GPIO_PIN_14
#define LCD_SPI_GPIO_Port   GPIOG

/* ---------------------------------------------------------------------------
 * Convenience macros
 * -------------------------------------------------------------------------*/
#define LED_ON()            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET)
#define LED_OFF()           HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET)
#define LED_TOGGLE()        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin)

#define LCD_DC_DATA()       HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET)
#define LCD_DC_COMMAND()    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET)

#define LCD_BL_OFF()        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET)
#define LCD_BL_ON()         HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET)

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
