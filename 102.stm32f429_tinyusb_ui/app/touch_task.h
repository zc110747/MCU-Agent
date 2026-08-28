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

/* How long the task sleeps on the semaphore between idle polls.  The T_PEN
 * interrupt is the real trigger; this timeout is only a safety net so a panel
 * whose INT line is not wired still reports touches (slower, but working). */
#define TOUCH_IDLE_POLL_MS       1000U

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
