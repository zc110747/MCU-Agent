/**
 ******************************************************************************
 * @file    main.h
 * @brief   Board level definitions for the STM32H743ZIT6 UVC camera project.
 ******************************************************************************
 */

#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * Video format
 *
 * The OV5640 runs a 4:3 ISP window and outputs 400x300 YUV422 (YUYV).  The
 * DCMI crop unit then extracts the centred 240x240 window, which is exactly
 * what we stream to the host as UVC YUY2.
 *
 * 400x300 is not arbitrary: it keeps the sensor on the 4:3 geometry that the
 * PLL, ISP window and binning settings in ov5640_ref.c were validated with.
 * Retuning the sensor to output 240x240 directly would change the pre-scaler
 * ratio and re-open the sub-sampling problems this configuration solved.
 * ========================================================================== */
#define CAM_SENSOR_WIDTH    400U
#define CAM_SENSOR_HEIGHT   300U

#define FRAME_WIDTH         240U
#define FRAME_HEIGHT        240U
#define FRAME_BYTES_PER_PX  2U /* YUY2 */
#define FRAME_SIZE          (FRAME_WIDTH * FRAME_HEIGHT * FRAME_BYTES_PER_PX) /* 115200 */

/* Announced frame rate. USB FS isochronous gives ~1023 B/ms => ~8 fps max. */
#define FRAME_RATE          8U

/* ==========================================================================
 * Pin map (Luxiaoban STM32H743ZIT6 board)
 * ========================================================================== */

/* Run/heartbeat LED */
#define LED_RUN_PIN         GPIO_PIN_7
#define LED_RUN_PORT        GPIOG
#define LED_RUN_CLK_ENABLE() __HAL_RCC_GPIOG_CLK_ENABLE()

/* OV5640 control */
#define CAM_PWDN_PIN        GPIO_PIN_13
#define CAM_PWDN_PORT       GPIOF
#define CAM_PWDN_CLK_ENABLE() __HAL_RCC_GPIOF_CLK_ENABLE()

/* SCCB / I2C4 : PF14 = SCL, PF15 = SDA */
#define CAM_I2C_INSTANCE    I2C4
#define CAM_I2C_SCL_PIN     GPIO_PIN_14
#define CAM_I2C_SDA_PIN     GPIO_PIN_15
#define CAM_I2C_PORT        GPIOF
#define CAM_I2C_AF          GPIO_AF4_I2C4

/* OV5640 SCCB address (8-bit form, as expected by the ST component driver) */
#define CAM_I2C_ADDRESS     0x78U

/* ==========================================================================
 * DCMI pin map
 *   PA4  DCMI_HSYNC     PA6  DCMI_PIXCLK   PG9  DCMI_VSYNC
 *   PC6  D0   PC7  D1   PG10 D2   PG11 D3
 *   PE4  D4   PD3  D5   PE5  D6   PE6  D7
 * ========================================================================== */

/* Frame buffers live in AXI SRAM (D1) and are marked non-cacheable by the MPU */
#define FRAMEBUF_BASE_ADDR  0x24000000UL

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
