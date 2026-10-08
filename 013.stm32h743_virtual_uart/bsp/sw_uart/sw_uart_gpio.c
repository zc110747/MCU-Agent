/* ---------------------------------------------------------------------------
 * Software UART - GPIO abstraction.
 * -------------------------------------------------------------------------*/

#include "sw_uart_gpio.h"
#include "stm32h7xx_hal.h"

/* ------------------------------------------------------------------------ */
void sw_uart_gpio_init(void)
{
    GPIO_InitTypeDef g = {0};

    SW_UART_TX_CLK_ENABLE();
    SW_UART_RX_CLK_ENABLE();

    /* TX: push-pull output, parked at the idle level (1) so the line reads
     * high before the first start bit. */
    g.Pin   = SW_UART_TX_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SW_UART_TX_PORT, &g);
    sw_uart_gpio_tx_set();

    /* RX: floating input with a pull-up, so an unplugged line is read as the
     * idle 1 instead of chattering. EXTI is armed for the falling edge, which
     * is the only edge a start bit can produce. */
    g.Pin  = SW_UART_RX_PIN;
    g.Mode = GPIO_MODE_IT_FALLING;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SW_UART_RX_PORT, &g);
}

/* ------------------------------------------------------------------------ */
void sw_uart_gpio_tx_set(void)
{
    /* Bit position selects set/reset; single 32-bit store, no RMW race. */
    SW_UART_TX_PORT->BSRR = SW_UART_TX_BSRR_SET;
}

void sw_uart_gpio_tx_reset(void)
{
    SW_UART_TX_PORT->BSRR = SW_UART_TX_BSRR_RESET;
}

/* ------------------------------------------------------------------------ */
uint8_t sw_uart_gpio_rx_read(void)
{
    return (uint8_t)(SW_UART_RX_PORT->IDR & SW_UART_RX_PIN);
}

uint8_t sw_uart_gpio_tx_read(void)
{
    /* IDR reads the pad, not the output latch, so this reflects whatever the
     * DMA has actually driven onto the pin. */
    return (uint8_t)((SW_UART_TX_PORT->IDR & SW_UART_TX_PIN) ? 1U : 0U);
}
