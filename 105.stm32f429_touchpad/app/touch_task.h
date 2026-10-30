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

/* Poll period while a contact is being tracked.  The GT9147 refreshes its
 * coordinates every ~10 ms, so ~5 ms is enough to pick up every new frame with
 * at most half a frame of phase error; a longer period directly shows up as
 * pointer lag, because this loop is what feeds usb_mouse_move().
 *
 * The bit-banged I2C burst costs ~0.6 ms per single-contact scan, i.e. ~12 % of
 * one core at this rate.  Do not go below 4 ms: the bus is polled, not DMA'd. */
#define TOUCH_POLL_MS            5U

/* Consecutive *valid* release frames before the contact counts as finished
 * (4 x 5 ms).  Only a frame whose status register had the buffer-ready bit set
 * and reported zero points counts here - a poll that merely found no new frame
 * is not a release (see BSP_TOUCH_NO_FRAME). */
#define TOUCH_RELEASE_POLLS      4U

/* How long the controller may stay silent mid-contact before the contact is
 * declared lost.  The GT9xx normally reports every ~10 ms, so this only fires
 * when a finger was resting perfectly still and the chip stopped refreshing,
 * or when a frame got lost.  It must stay well above the report period, and
 * low enough that a stale contact cannot turn the next touch into a jump. */
#define TOUCH_STALL_MS           120U

/* Upper bound on one contact (3000 x 5 ms = 15 s) so a phantom contact can
 * never keep this task spinning for ever.  It used to be 800 polls (4 s), which
 * a normal slow drag exceeds - the loop then "aborted" a perfectly good stroke
 * and re-armed the interrupt in the middle of it. */
#define TOUCH_MAX_POLLS          3000U

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
