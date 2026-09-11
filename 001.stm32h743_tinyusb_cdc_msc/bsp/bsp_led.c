/* ---------------------------------------------------------------------------
 * On-board user LED driver (see bsp_led.h)
 * -------------------------------------------------------------------------*/

#include "bsp_led.h"
#include "stm32h7xx_hal.h"

static void led_init(void) {
  GPIO_InitTypeDef g = {0};
  BSP_LED_GPIO_CLK_EN();
  g.Pin   = BSP_LED_GPIO_PIN;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BSP_LED_GPIO_PORT, &g);
  bsp_led_write(false);
}

void bsp_led_init(void) {
  led_init();
}

void bsp_led_write(bool on) {
#if BSP_LED_ACTIVE_HIGH
  HAL_GPIO_WritePin(BSP_LED_GPIO_PORT, BSP_LED_GPIO_PIN,
                    on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
  HAL_GPIO_WritePin(BSP_LED_GPIO_PORT, BSP_LED_GPIO_PIN,
                    on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif
}

void bsp_led_toggle(void) {
  HAL_GPIO_TogglePin(BSP_LED_GPIO_PORT, BSP_LED_GPIO_PIN);
}
