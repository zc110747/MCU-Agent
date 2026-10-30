/**
 ******************************************************************************
 * @file    touch_task.c
 * @brief   Touch acquisition: wakes on T_PEN, polls the GT9147, feeds gestures.
 *
 *  Why it polls after the interrupt
 *  --------------------------------
 *  Some GT9147 modules only pulse INT once per contact, others pulse it for the
 *  whole contact.  Waiting purely on edges would therefore miss either the
 *  release or the movement.  The interrupt is only the wake-up trigger; the
 *  task then polls every TOUCH_POLL_MS until the controller stops reporting
 *  contacts, which gives correct press/move/release in both cases.
 *
 *  Ownership
 *  ---------
 *  This task is the ONLY caller of bsp_touch_scan(), i.e. the only user of the
 *  bit-banged I2C bus.  It publishes
 *    - all contacts  -> gesture engine  (drawing / taps / scroll / pan)
 *    - first contact -> bsp_touch_get() (LVGL pointer input)
 *  so neither the widget layer nor the drawing code ever waits on I2C.
 ******************************************************************************
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_touch.h"
#include "gesture.h"

#include "touch_task.h"
#include "log.h"

void touch_task(void *arg)
{
    bsp_touch_point_t raw[BSP_TOUCH_MAX_POINTS];
    gesture_point_t   pts[GESTURE_MAX_POINTS];
    uint8_t  storm_logged = 0U;
    uint8_t  err_logged   = 0U;
    uint32_t irq_window_at   = 0U;
    uint32_t irq_window_base = 0U;

    (void)arg;

    PRINT_LOG("[TOUCH] task started (T_SCK=PH6 T_MOSI=PI3 T_CS=PI8 T_PEN=PH7)\r\n");

    while (bsp_touch_init() != 0)
    {
        PRINT_LOG("[TOUCH] controller not answering, retrying in %u ms\r\n",
                  (unsigned int)TOUCH_RETRY_MS);
        vTaskDelay(pdMS_TO_TICKS(TOUCH_RETRY_MS));
    }

    for (;;)
    {
        uint32_t polls = 0U;
        uint32_t idle  = 0U;

        (void)bsp_touch_wait(pdMS_TO_TICKS(TOUCH_IDLE_POLL_MS));

        /* ---- Interrupt sanity check, evaluated once per second ---------- */
        if (irq_window_at == 0U)
        {
            irq_window_at   = HAL_GetTick();
            irq_window_base = bsp_touch_irq_count();
        }
        else if ((HAL_GetTick() - irq_window_at) >= 1000U)
        {
            uint32_t rate = bsp_touch_irq_count() - irq_window_base;

            if ((storm_logged == 0U) && (rate > TOUCH_IRQ_STORM_HZ))
            {
                storm_logged = 1U;
                PRINT_LOG("[TOUCH] WARNING: T_PEN storm %lu/s - check PH7 "
                          "pull-up / crosstalk from PH6\r\n",
                          (unsigned long)rate);
            }
            irq_window_at   = HAL_GetTick();
            irq_window_base = bsp_touch_irq_count();
        }

        /* ---- Track the contact until release ---------------------------- */
        while ((idle < TOUCH_RELEASE_POLLS) && (polls < TOUCH_MAX_POLLS))
        {
            int n = bsp_touch_scan(raw, (uint8_t)BSP_TOUCH_MAX_POINTS);
            uint8_t i;
            uint8_t cnt;

            polls++;

            if (n < 0)
            {
                /* Controller stopped answering mid-contact.  Log once per
                 * occurrence: if the bus is dead this loop would otherwise
                 * print 20 lines/s for ever. */
                if (err_logged == 0U)
                {
                    PRINT_LOG("[TOUCH] controller stopped answering (poll %lu) "
                              "- recovering\r\n", (unsigned long)polls);
                    err_logged = 1U;
                }
                break;
            }

            cnt = (uint8_t)n;
            for (i = 0U; i < cnt; i++)
            {
                pts[i].x = raw[i].x;
                pts[i].y = raw[i].y;
            }

            gesture_feed(HAL_GetTick(), pts, cnt);

            if (n > 0)
            {
                idle = 0U;
            }
            else
            {
                idle++;
            }

            vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
        }

        /* Fresh session: allow the next failure to be logged again. */
        err_logged = 0U;

        /* Hit the contact cap without ever seeing a release, or the controller
         * died mid-contact: still close any open stroke so the UI is not left
         * with a line that never ends. */
        if (idle < TOUCH_RELEASE_POLLS)
        {
            gesture_feed(HAL_GetTick(), NULL, 0U);
            PRINT_LOG("[TOUCH] contact aborted after %lu polls\r\n",
                      (unsigned long)polls);
        }

        /* Debounce guard before re-arming, so a noisy line cannot retrigger the
         * ISR the instant it is unmasked. */
        vTaskDelay(pdMS_TO_TICKS(TOUCH_IRQ_REARM_MS));
        bsp_touch_irq_rearm();
    }
}
