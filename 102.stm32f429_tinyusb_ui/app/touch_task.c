/**
  ******************************************************************************
  * @file    touch_task.c
  * @brief   Touch detection thread: wakes on T_PEN and polls the GT9147.
  *
  *  Why it polls after the interrupt
  *  --------------------------------
  *  Some GT9147 modules only pulse INT once per contact, others pulse it for
  *  the whole contact.  Waiting purely on edges would therefore miss either
  *  the release or the movement.  The interrupt is only the wake-up trigger;
  *  the task then polls every TOUCH_POLL_MS until the controller stops
  *  reporting contacts, which gives correct press/move/release in both cases.
  ******************************************************************************
  */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_touch.h"

#include "touch_task.h"
#include "log.h"

void touch_task(void *arg)
{
    (void)arg;
    uint32_t idle_polls = 0U;
    uint32_t polls      = 0U;
    uint8_t  wake_logged  = 0U;
    uint8_t  storm_logged = 0U;
    uint32_t irq_window_at = 0U;
    uint32_t irq_window_base = 0U;

    PRINT_LOG("[TOUCH] task started, probing GT9147 (T_SCK=PH6 T_MOSI=PI3 "
           "T_CS=PI8 T_PEN=PH7)\r\n");

    if (bsp_touch_init() != 0)
    {
        PRINT_LOG("[TOUCH] disabled: controller not answering\r\n");
        for (;;)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }

    PRINT_LOG("[TOUCH] INT armed on T_PEN (PH7, falling edge) -> waiting\r\n");

    for (;;)
    {
        int signalled;
        uint32_t irq_before = bsp_touch_irq_count();

        /* Block until the GT9147 pulls T_PEN low.  The timeout is a safety
         * net: if the INT line is not wired the loop still polls once per
         * second and touch keeps working, just without the interrupt. */
        signalled = bsp_touch_wait(pdMS_TO_TICKS(TOUCH_IDLE_POLL_MS));

        if ((signalled != 0) && (wake_logged == 0U))
        {
            /* One-shot proof that the full chain works: T_PEN edge -> EXTI ->
             * ISR -> semaphore -> this task. */
            wake_logged = 1U;
            PRINT_LOG("[TOUCH] T_PEN interrupt received (irq=%lu)\r\n",
                   (unsigned long)bsp_touch_irq_count());
        }

        /* Interrupt sanity check, evaluated once per second.  A real GT9xx
         * reports at ~100 Hz; a rate in the kHz range means the INT line is
         * picking up noise and the storm will starve the lower-priority I2C2
         * sensor task (which is exactly how its bus got wedged). */
        if ((storm_logged == 0U) && (irq_window_at == 0U))
        {
            irq_window_at   = HAL_GetTick();
            irq_window_base = bsp_touch_irq_count();
        }
        else if ((storm_logged == 0U) &&
                 ((HAL_GetTick() - irq_window_at) >= 1000U))
        {
            uint32_t rate = bsp_touch_irq_count() - irq_window_base;

            if (rate > TOUCH_IRQ_STORM_HZ)
            {
                storm_logged = 1U;
                PRINT_LOG("[TOUCH] WARNING: T_PEN interrupt storm %lu/s "
                       "(> %lu/s) - check PH7 pull-up / crosstalk from PH6\r\n",
                       (unsigned long)rate, (unsigned long)TOUCH_IRQ_STORM_HZ);
            }
            irq_window_at   = HAL_GetTick();
            irq_window_base = bsp_touch_irq_count();
        }

        idle_polls = 0U;
        polls      = 0U;
        while ((idle_polls < TOUCH_RELEASE_POLLS) && (polls < TOUCH_MAX_POLLS))
        {
            polls++;
            if (bsp_touch_scan() > 0)
            {
                idle_polls = 0U;
                if ((signalled == 0) && (bsp_touch_irq_count() == irq_before))
                {
                    /* A contact was found without any T_PEN edge.  Report it
                     * once - this is the symptom of a missing INT connection. */
                    PRINT_LOG("[TOUCH] WARNING: contact seen without T_PEN "
                           "interrupt (fallback poll only)\r\n");
                    irq_before = 0xFFFFFFFFU;   /* print at most once */
                }
            }
            else
            {
                idle_polls++;
            }
            vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
        }

        /* Hit the cap without ever seeing a release: the panel is reporting a
         * stuck/phantom contact.  Say so once per occurrence and go back to
         * waiting on the semaphore - the poll loop must never spin for ever. */
        if (polls >= TOUCH_MAX_POLLS)
        {
            PRINT_LOG("[TOUCH] WARNING: contact held for %lu ms without "
                   "release (phantom touch?) - re-arming\r\n",
                   (unsigned long)(TOUCH_MAX_POLLS * TOUCH_POLL_MS));
        }

        /* Debounce guard before re-arming the interrupt, so a line that is
         * picking up noise cannot retrigger the ISR the instant we unmask it.
         * The poll loop above would see a real touch in the meantime anyway. */
        vTaskDelay(pdMS_TO_TICKS(TOUCH_IRQ_REARM_MS));
        bsp_touch_irq_rearm();
    }
}
