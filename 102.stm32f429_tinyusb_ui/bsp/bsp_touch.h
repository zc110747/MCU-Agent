/**
  ******************************************************************************
  * @file    bsp_touch.h
  * @brief   Touch service: GT9147 + T_PEN interrupt + FreeRTOS signalling.
  *
  *  Signal chain
  *  ------------
  *      GT9147 INT (T_PEN = PH7)
  *        -> EXTI line 7, falling edge
  *        -> HAL_EXTI pending callback (ISR context)
  *        -> xSemaphoreGiveFromISR on a binary semaphore
  *        -> touch_task() wakes up and polls the controller
  *
  *  The interrupt only wakes the task; it never touches the I2C bus from ISR
  *  context.  The task keeps polling while a finger is down because not every
  *  module pulses INT for the whole contact - polling is what guarantees the
  *  release is seen.
  *
  *  Coordinate mapping
  *  ------------------
  *  The chip is configured for a 480 x 800 touch area, which is exactly the
  *  GRAM window the NT35510 panel is driven with, so the default mapping is
  *  the identity.  If a panel is mounted the other way round the three macros
  *  below are the single place to change.
  ******************************************************************************
  */
#ifndef __BSP_TOUCH_H__
#define __BSP_TOUCH_H__

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/* ---- Screen orientation --------------------------------------------------- */
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY   0       /* 1: exchange the raw X and Y axes          */
#endif
#ifndef TOUCH_INVERT_X
#define TOUCH_INVERT_X  0       /* 1: mirror the horizontal axis             */
#endif
#ifndef TOUCH_INVERT_Y
#define TOUCH_INVERT_Y  0       /* 1: mirror the vertical axis               */
#endif

/* EXTI line used for T_PEN.  Must be numerically >=
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5) so the FromISR give is
 * legal; 6 puts it just below the USB host interrupt in urgency. */
#define TOUCH_EXTI_PREEMPT_PRIO  6U

/* Handle used by EXTI9_5_IRQHandler (see app/stm32f4xx_it.c). */
extern EXTI_HandleTypeDef g_touch_exti;

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Bring up the GT9147 and the T_PEN interrupt.  Must run from a task
  *         (it prints and uses busy-wait delays).
  *  @retval 0 ready, -1 no touch controller found.
  */
int bsp_touch_init(void);

/**
  * @brief  @retval 1 when the controller answered during init.
  */
int bsp_touch_is_ready(void);

/**
  * @brief  Block until T_PEN signals activity.
  *  @retval 1 semaphore taken (touch activity), 0 timed out.
  */
int bsp_touch_wait(TickType_t ticks);

/**
  * @brief  Poll the controller once and publish the result for LVGL.
  *  @retval number of contacts (>=1 = pressed), 0 = released, -1 = not ready.
  */
int bsp_touch_scan(void);

/**
  * @brief  Current published state (last scan).
  */
void bsp_touch_get(uint16_t *x, uint16_t *y, uint8_t *pressed);

/**
  * @brief  Number of T_PEN interrupts seen since boot.
  */
uint32_t bsp_touch_irq_count(void);

/**
  * @brief  Number of scans that reported at least one contact.
  */
uint32_t bsp_touch_press_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TOUCH_H__ */
