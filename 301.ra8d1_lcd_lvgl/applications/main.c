/**
 * @file main.c
 * @brief Phase 0.5 minimal bare-metal app: clock init + LED blink
 */
#include <stdint.h>

#include "bsp_led.h"

#define BLINK_DELAY_LOOPS    (20000000U)

static void busy_delay(volatile uint32_t loops);

int entry(void)
{
    bsp_led_init();

    while (1)
    {
        bsp_led_toggle();
        busy_delay(BLINK_DELAY_LOOPS);
    }

    return 0;
}

static void busy_delay(volatile uint32_t loops)
{
    while (loops > 0U)
    {
        loops--;
    }
}
