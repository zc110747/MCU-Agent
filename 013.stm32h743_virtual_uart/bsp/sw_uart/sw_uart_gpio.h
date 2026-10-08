/* ---------------------------------------------------------------------------
 * Software UART - GPIO abstraction.
 *
 * The whole point of this layer is that no other module names a port or pin:
 * TX/RX/DMA only see sw_uart_gpio_* calls, so moving the wiring is a
 * one-file edit in sw_uart_config.h.
 *
 * Bit access goes exclusively through BSRR. A read-modify-write on ODR would
 * race with the TX DMA - both drive the same register, and a CPU store that
 * lands between a DMA set and clear would corrupt the output. BSRR makes each
 * update atomic because the bit position selects set/reset and the write is a
 * single 32-bit store.
 * -------------------------------------------------------------------------*/
#ifndef SW_UART_GPIO_H
#define SW_UART_GPIO_H

#include <stdint.h>
#include "sw_uart_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Drive TX low (start bit) and park RX as an input with EXTI on both edges. */
void sw_uart_gpio_init(void);

/* Set or reset the TX pin through BSRR. */
void sw_uart_gpio_tx_set(void);
void sw_uart_gpio_tx_reset(void);

/* Read the live level of the RX pin. */
uint8_t sw_uart_gpio_rx_read(void);

#ifdef __cplusplus
}
#endif

#endif /* SW_UART_GPIO_H */
