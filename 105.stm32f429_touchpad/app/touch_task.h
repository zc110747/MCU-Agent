/**
 ******************************************************************************
 * @file    touch_task.h
 * @brief   Touch acquisition thread: T_PEN interrupt -> GT9147 poll -> gesture.
 ******************************************************************************
 */
#ifndef __TOUCH_TASK_H__
#define __TOUCH_TASK_H__

/* Stack size in words (4 bytes each).  The task only does small I2C reads. */
#define TOUCH_TASK_STACK_WORDS   512U

/* Above the UI pump so a press is published before the next LVGL read. */
#define TOUCH_TASK_PRIO          (tskIDLE_PRIORITY + 4U)

/* Poll period while a contact is being tracked (~100 Hz, matches the panel). */
#define TOUCH_POLL_MS            10U

/* Consecutive empty polls before the contact counts as finished. */
#define TOUCH_RELEASE_POLLS      3U

/* Upper bound on one contact (400 x 10 ms = 4 s) so a phantom contact can never
 * keep this task spinning for ever. */
#define TOUCH_MAX_POLLS          400U

/* Safety-net wake-up while idle.  The T_PEN interrupt is the real trigger; this
 * timeout keeps touch working (slower) if the INT line is not wired. */
#define TOUCH_IDLE_POLL_MS       50U

/* Interrupt storm threshold (1 s window).  A real GT9xx reports at ~100 Hz;
 * anything above this means PH7 is picking up noise from the LCD/I2C lines. */
#define TOUCH_IRQ_STORM_HZ       1000U

/* How long the interrupt stays masked after a contact before it is re-armed. */
#define TOUCH_IRQ_REARM_MS       5U

/* Retry period when the controller did not answer at boot. */
#define TOUCH_RETRY_MS           2000U

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Touch acquisition thread body.
  *
  *  1. initialise the GT9147 and the T_PEN EXTI
  *  2. block on the binary semaphore given by the T_PEN ISR
  *  3. poll the controller until it reports no contact, feeding every sample to
  *     the gesture engine (app/gesture.c)
  */
void touch_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* __TOUCH_TASK_H__ */
