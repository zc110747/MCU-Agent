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
#include "trackpad.h"

#include "touch_task.h"
#include "log.h"

void touch_task(void *arg)
{
    bsp_touch_point_t  raw[BSP_TOUCH_MAX_POINTS];
    gesture_point_t    pts[GESTURE_MAX_POINTS];
    trackpad_point_t   tpts[TRACKPAD_MAX_POINTS];
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
        uint32_t last_frame_ms = 0U;

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
        /* Anchor the silence watchdog at the wake-up, otherwise the first
         * between-frames poll would compare against the boot tick and declare
         * the contact lost immediately. */
        last_frame_ms = HAL_GetTick();

        while ((idle < TOUCH_RELEASE_POLLS) && (polls < TOUCH_MAX_POLLS))
        {
            int n = bsp_touch_scan(raw, (uint8_t)BSP_TOUCH_MAX_POINTS);
            uint8_t i;
            uint8_t cnt;

            polls++;

            /* ---- No new frame from the controller ----------------------- *
             * The GT9xx only updates 0x814E once per report period (~10 ms),
             * while this loop samples every 5 ms, so half of the polls land
             * between two frames.  That is neither a press nor a release: the
             * finger simply has not moved into a new frame yet.  Treating it
             * as a release (which is what returning 0 used to mean) ended the
             * stroke after a single sample, so every contact produced one
             * zero-length delta and the pointer never moved. */
            if (n == BSP_TOUCH_NO_FRAME)
            {
                if ((HAL_GetTick() - last_frame_ms) >= TOUCH_STALL_MS)
                {
                    /* Really silent for too long: the controller lost the
                     * contact (or the finger lifted without a release frame).
                     * Close the stroke so the host does not keep a button
                     * stuck and the next touch cannot look like a big jump. */
                    trackpad_feed(NULL, 0U);
                    gesture_feed(HAL_GetTick(), NULL, 0U);
                    idle = TOUCH_RELEASE_POLLS;
                }

                vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
                continue;
            }

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

                /* Do not leave the pointer logically pressed: the host would
                 * keep whatever button state we last reported. */
                trackpad_feed(NULL, 0U);
                break;
            }

            /* A real frame arrived: it carries either new coordinates or the
             * definitive "all fingers up" of the release. */
            last_frame_ms = HAL_GetTick();

            cnt = (uint8_t)n;
            for (i = 0U; i < cnt; i++)
            {
                pts[i].x    = raw[i].x;
                pts[i].y    = raw[i].y;
                pts[i].id   = raw[i].id;
                pts[i].size = raw[i].size;

                tpts[i].x   = raw[i].x;
                tpts[i].y   = raw[i].y;
                tpts[i].id  = raw[i].id;
            }

            /* Pointer first: it is what the user feels.  It is fed the WHOLE
             * sample, not just the first contact, because the engine has to see
             * how many fingers are down: with two it freezes the pointer
             * instead of dragging it along the scroll.  The gesture engine runs
             * afterwards because its state machine costs more than the two
             * comparisons trackpad_feed() does. */
            trackpad_feed(tpts, cnt);
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
         * with a line that never ends, and release the virtual contact so the
         * pointer does not stay "pressed" across the gap. */
        if (idle < TOUCH_RELEASE_POLLS)
        {
            trackpad_feed(NULL, 0U);
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
