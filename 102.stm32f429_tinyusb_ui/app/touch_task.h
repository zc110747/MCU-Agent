/**
  ******************************************************************************
  * @file    touch_task.h
  * @brief   Touch detection thread: wakes on T_PEN and polls the GT9147.
  ******************************************************************************
  */
#ifndef __TOUCH_TASK_H__
#define __TOUCH_TASK_H__

/* Stack size in words (4 bytes each).  The task only does small I2C reads. */
#define TOUCH_TASK_STACK_WORDS   512U

/* Task priority: above the UI pump so a press is published before the next
 * LVGL read, below the USB host stack. */
#define TOUCH_TASK_PRIO          (tskIDLE_PRIORITY + 4U)

/* Poll period while a contact is being tracked. */
#define TOUCH_POLL_MS            15U

/* Number of consecutive empty polls before the contact is considered over. */
#define TOUCH_RELEASE_POLLS      3U

/* Upper bound on how long one contact is tracked before the loop gives up and
 * goes back to waiting on the semaphore.  A panel that reports a phantom touch
 * (seen here: a stuck contact at the last button pressed, while the INT line
 * was picking up ~15 kHz of crosstalk) would otherwise keep the loop spinning
 * for ever.  200 polls x 15 ms = 3 s of continuous contact. */
#define TOUCH_MAX_POLLS          200U

/* How long the task sleeps on the semaphore between idle polls.  The T_PEN
 * interrupt is the real trigger; this timeout is only a safety net so a panel
 * whose INT line is not wired still reports touches (slower, but working). */
#define TOUCH_IDLE_POLL_MS       1000U

/* Interrupt sanity threshold.  A real GT9xx reports at ~100 Hz; anything above
 * TOUCH_IRQ_STORM_HZ means the INT line is picking up noise (floating pin,
 * crosstalk from the adjacent bit-banged SCL, or the FMC/LCD parallel bus) and
 * the storm will starve the lower-priority I2C2 sensor task.  Warn once so it
 * shows up in the log.  Measured on this board: 47 000/s. */
#define TOUCH_IRQ_STORM_HZ       1000U

/* Debounce guard: how long the touch interrupt stays masked after it fired,
 * before the task re-arms it.  Caps the ISR rate at 1/(poll loop + guard)
 * regardless of how noisy the line is (~20 Hz here instead of ~47 kHz). */
#define TOUCH_IRQ_REARM_MS       5U

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Touch detection thread body.
  *
  *  1. initialise the GT9147 and the T_PEN EXTI
  *  2. block on the binary semaphore given by the T_PEN ISR
  *  3. on wake-up, poll the controller until it reports no contact
  */
void touch_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* __TOUCH_TASK_H__ */
