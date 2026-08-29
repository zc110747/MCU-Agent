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
    uint8_t  wake_logged = 0U;

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

        idle_polls = 0U;
        while (idle_polls < TOUCH_RELEASE_POLLS)
        {
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
    }
}
