/**
 * Minimal FreeRTOS surface for host-side unit tests.
 *
 * tools/test_trackpad_tap.c compiles app/trackpad.c with the host gcc, and that
 * file only needs a tick counter plus the critical-section macros.  This header
 * provides exactly those, no more - if the firmware ever starts using something
 * else from FreeRTOS inside the tested module, the host build fails and that is
 * the signal to extend this stub (instead of silently diverging from the target).
 */
#ifndef __HOST_FREERTOS_H__
#define __HOST_FREERTOS_H__

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(x)            ((TickType_t)(x))
#define tskIDLE_PRIORITY            0U

/* Implemented by the test: a plain counter, so the test can also assert that
 * every critical section was properly closed again. */
void host_enter_critical(void);
void host_exit_critical(void);

#define taskENTER_CRITICAL()        host_enter_critical()
#define taskEXIT_CRITICAL()         host_exit_critical()

#endif /* __HOST_FREERTOS_H__ */
