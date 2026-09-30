/**
 * @file bsp_led.c
 * @brief On-board LED (P102) driver for RA8D1 Vision Board
 *
 * LED active level is pending schematic confirmation; drive-both assumed safe.
 */
#include "bsp_led.h"

#include "bsp_api.h"

/* LED pin: P102, see documents/pinmap.md */
#define BSP_LED_PIN    BSP_IO_PORT_01_PIN_02

typedef struct
{
    bool on;
} led_info_t;

static led_info_t g_led_info = { false };

void bsp_led_init(void)
{
    R_BSP_PinAccessEnable();
    R_BSP_PinWrite(BSP_LED_PIN, false);
    g_led_info.on = false;
}

void bsp_led_write(bool on)
{
    R_BSP_PinWrite(BSP_LED_PIN, on);
    g_led_info.on = on;
}

void bsp_led_toggle(void)
{
    g_led_info.on = !g_led_info.on;
    R_BSP_PinWrite(BSP_LED_PIN, g_led_info.on);
}
