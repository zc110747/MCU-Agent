/**
  ******************************************************************************
  * @file    main.h
  * @brief   Board level definitions for LXB743ZI-P1 (STM32H743ZIT6).
  *
  * Pin map (from LXB743ZI-P1 schematic / CubeMX .ioc):
  *   LED       : PG7   (push-pull output, active high)
  *   LCD_BL    : PG12  (OLED backlight, active high)
  *   LCD_DC    : PG15  (OLED data/command select)
  *   SPI6_NSS  : PG8   (hardware NSS)
  *   SPI6_SCK  : PG13
  *   SPI6_MOSI : PG14  (1-line simplex master, TX only)
  *   SDMMC1    : PC8..PC12 (D0..D3, CK), PD2 (CMD), 4-bit bus
  *   USART1    : PA9 (TX) / PA10 (RX), 115200-8-N-1
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

/* Exported types ------------------------------------------------------------*/
typedef enum
{
    RT_OK = 0,
    RT_FAIL,
} GlobalType_t;

#ifndef UINT_MAX
#define UINT_MAX 0xFFFFFFFF
#endif

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void SystemClock_Config(void);

/* Peripheral handles shared across the project -------------------------------*/
extern SPI_HandleTypeDef  hspi6;
extern SD_HandleTypeDef   hsd1;
extern UART_HandleTypeDef huart1;

/* Private defines -----------------------------------------------------------*/
#define LED_Pin             GPIO_PIN_7
#define LED_GPIO_Port       GPIOG
#define LCD_BL_Pin          GPIO_PIN_12
#define LCD_BL_GPIO_Port    GPIOG
#define LCD_DC_Pin          GPIO_PIN_15
#define LCD_DC_GPIO_Port    GPIOG

#define LED_ON()            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET)
#define LED_OFF()           HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET)
#define LED_TOGGLE()        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin)

#define LCD_DC_DATA()       HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET)
#define LCD_DC_COMMAND()    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET)

/* Backlight: low = off, high = on */
#define LCD_BL_OFF()        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET)
#define LCD_BL_ON()         HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET)

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
