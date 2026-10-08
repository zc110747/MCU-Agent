/* ---------------------------------------------------------------------------
 * Software UART - compile-time configuration.
 *
 * Every hardware pin, buffer size and peripheral instance used by the driver
 * is selected here. Nothing else in bsp/sw_uart/ names a GPIO port, a pin or
 * a TIM/DMA handle, so retargeting the driver is a one-file edit.
 *
 * Frame format is fixed at 8 data bits, 1 stop bit, no parity (10 bit times
 * per byte). The 115200 entry is reserved: the arithmetic in
 * sw_uart_timer.c supports it, but only 9600 has been verified on hardware.
 * -------------------------------------------------------------------------*/
#ifndef SW_UART_CONFIG_H
#define SW_UART_CONFIG_H

#include "stm32h7xx.h"

/* ---------------------------------------------------------------------------
 * Pin assignment - pure GPIO, no USART alternate function is ever configured.
 *
 * Wiring (matches the USART1 pinout of the MCU side):
 *   PA9  = MCU TX -> host RX
 *   PA10 = MCU RX <- host TX
 * -------------------------------------------------------------------------*/
#define SW_UART_TX_PORT           GPIOA
#define SW_UART_TX_PIN            GPIO_PIN_9
#define SW_UART_TX_PIN_NUM        9U
#define SW_UART_TX_CLK_ENABLE()   __HAL_RCC_GPIOA_CLK_ENABLE()

#define SW_UART_RX_PORT           GPIOA
#define SW_UART_RX_PIN            GPIO_PIN_10
#define SW_UART_RX_PIN_NUM        10U
#define SW_UART_RX_CLK_ENABLE()   __HAL_RCC_GPIOA_CLK_ENABLE()

/* BSRR atomic set/reset words derived from the pin number - these are what the
 * DMA writes straight into the GPIO register, so they must stay in sync with
 * SW_UART_TX_PIN_NUM. */
#define SW_UART_TX_BSRR_SET       (1UL << SW_UART_TX_PIN_NUM)
#define SW_UART_TX_BSRR_RESET     (1UL << (SW_UART_TX_PIN_NUM + 16U))

/* ---------------------------------------------------------------------------
 * Buffer sizes
 * -------------------------------------------------------------------------*/
#define SW_UART_TX_BUFFER_SIZE    4096U
#define SW_UART_RX_BUFFER_SIZE    1024U

/* One UART byte is encoded as this many GPIO BSRR words. */
#define SW_UART_BITS_PER_BYTE     10U

/* ---------------------------------------------------------------------------
 * Supported baud rates
 * -------------------------------------------------------------------------*/
#define SW_UART_BAUD_9600         9600UL
#define SW_UART_BAUD_19200        19200UL
#define SW_UART_BAUD_38400        38400UL
#define SW_UART_BAUD_57600        57600UL
#define SW_UART_BAUD_115200       115200UL

#define SW_UART_DEFAULT_BAUD      SW_UART_BAUD_9600

/* ---------------------------------------------------------------------------
 * Timer instances
 *
 * TIM2 (APB1, 32-bit) generates the TX bit clock: its update event issues the
 * DMA request that writes one BSRR word per UART bit.
 * TIM3 (APB1, 32-bit) schedules the RX sampling points.
 * -------------------------------------------------------------------------*/
#define SW_UART_TX_TIMER          TIM2
#define SW_UART_TX_TIMER_CLK_ENABLE()  __HAL_RCC_TIM2_CLK_ENABLE()
#define SW_UART_TX_TIMER_IRQn     TIM2_IRQn

#define SW_UART_RX_TIMER          TIM3
#define SW_UART_RX_TIMER_CLK_ENABLE()  __HAL_RCC_TIM3_CLK_ENABLE()
#define SW_UART_RX_TIMER_IRQn     TIM3_IRQn

/* ---------------------------------------------------------------------------
 * DMA - TX bit stream
 *
 * H7 routes peripherals through DMAMUX, so the stream choice is free; the
 * request code is what actually selects the source. TIM2_UP = request 22.
 * -------------------------------------------------------------------------*/
#define SW_UART_TX_DMA_STREAM     DMA1_Stream0
#define SW_UART_TX_DMA_REQUEST    DMA_REQUEST_TIM2_UP
#define SW_UART_TX_DMA_IRQn       DMA1_Stream0_IRQn
#define SW_UART_TX_DMA_CLK_ENABLE()   __HAL_RCC_DMA1_CLK_ENABLE()

/* ---------------------------------------------------------------------------
 * Interrupt priorities (HAL_NVIC_SetPriority encoding: preempt, sub)
 *
 * Bit timing is the most timing-critical work in the system, so the TX DMA
 * completion and RX sampling interrupts sit at the highest preempt priority.
 * -------------------------------------------------------------------------*/
#define SW_UART_IRQ_PRIORITY      0U
#define SW_UART_IRQ_SUBPRIORITY   0U

#endif /* SW_UART_CONFIG_H */
