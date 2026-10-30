/**
  ******************************************************************************
  * @file    bsp_touch.h
   * @brief   Capacitive touch service for the 800x480 NT35510 module:
  *          GT9147/GT911 over bit-banged I2C + T_PEN EXTI + FreeRTOS signalling.
  *
  *  Signal chain (identical to 102.stm32f429_tinyusb_ui, hardware proven):
  *
  *      GT9147 INT (T_PEN = PH7) falling edge
  *        -> EXTI line 7 -> ISR (masks the line, gives a binary semaphore)
  *        -> touch_task wakes up and polls the controller over the bit-banged
  *           I2C bus (PH6 = SCL, PI3 = SDA)
  *
  *  The interrupt never talks to the bus; it only wakes the task.  The task
  *  keeps polling while the panel reports contacts, because not every GT9xx
  *  module pulses INT for the whole contact.
  *
  *  Unlike 102 this module publishes **all** contacts (up to
  *  BSP_TOUCH_MAX_POINTS) because the touchpad gesture engine needs two
  *  fingers for scrolling.  Coordinates are already mapped onto the LCD
  *  canvas, so a caller never has to deal with the chip's own resolution.
  ******************************************************************************
  */
#ifndef __BSP_TOUCH_H__
#define __BSP_TOUCH_H__

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/* Contacts reported per scan (GT9xx hardware maximum is 5). */
#define BSP_TOUCH_MAX_POINTS   5U

/* ---- Screen orientation --------------------------------------------------- */
/* The panel is driven as a 480x800 portrait GRAM window and the controller is
 * configured for exactly the same area, so the default mapping is the identity.
 * If a module reports a mirrored / swapped panel the three macros below are the
 * single place to change (see the raw/lv pair printed by touch_task). */
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY   0       /* 1: exchange the raw X and Y axes          */
#endif
#ifndef TOUCH_INVERT_X
#define TOUCH_INVERT_X  0       /* 1: mirror the horizontal axis             */
#endif
#ifndef TOUCH_INVERT_Y
#define TOUCH_INVERT_Y  0       /* 1: mirror the vertical axis               */
#endif

/* EXTI line used for T_PEN.  Priority must be numerically >=
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5) so the FromISR give is
 * legal. */
#define TOUCH_EXTI_PREEMPT_PRIO  6U

/* Handle used by EXTI9_5_IRQHandler (see app/stm32f4xx_it.c). */
extern EXTI_HandleTypeDef g_touch_exti;

typedef struct
{
    uint16_t x;     /* canvas X (0 .. canvas width  - 1) */
    uint16_t y;     /* canvas Y (0 .. canvas height - 1) */
} bsp_touch_point_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Bring up the GT9147/GT911 and the T_PEN interrupt.
  *         Waits (bounded) for the LCD driver to report its geometry, so it
  *         must run from a task, never before lcd_driver_init().
  *  @retval 0 ready, -1 controller or EXTI not available.
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
  * @brief  Poll the controller once and map up to @p max contacts onto the
  *         LCD canvas.  Masks the T_PEN interrupt for the duration of the
  *         bit-banged transaction and leaves it masked; the caller re-arms it
  *         with bsp_touch_irq_rearm() once it stops polling.
  *  @retval number of contacts written into @p pts (0 = released),
  *          -1 when the controller is not ready / not answering.
  */
int bsp_touch_scan(bsp_touch_point_t *pts, uint8_t max);

/**
  * @brief  Latest single-contact snapshot, for the LVGL pointer driver.
  *         Written by bsp_touch_scan() in the touch task and read by LVGL, so
  *         it is deliberately just three volatile words - no locking and no
  *         bus traffic on the render path.
  *  @param  x        canvas X of the first reported contact (may be NULL)
  *  @param  y        canvas Y of the first reported contact (may be NULL)
  *  @param  pressed  1 while at least one contact is down (may be NULL)
  */
void bsp_touch_get(uint16_t *x, uint16_t *y, uint8_t *pressed);

/**
  * @brief  Re-arm EXTI line 7 after the ISR (or a scan) masked it.  Drops any
  *         edge that arrived while masked so a noisy line cannot retrigger.
  */
void bsp_touch_irq_rearm(void);

/** Number of T_PEN interrupts seen since boot (storm / wiring diagnostics). */
uint32_t bsp_touch_irq_count(void);

/** Monotonic count of bsp_touch_scan() calls (sampling-rate diagnostics). */
uint32_t bsp_touch_scan_count(void);

/** Rolling average duration of one bsp_touch_scan(), in microseconds. */
uint32_t bsp_touch_scan_avg_us(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TOUCH_H__ */
