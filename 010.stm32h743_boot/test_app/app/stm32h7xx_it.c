/**
  ******************************************************************************
  * @file    test_app/app/stm32h7xx_it.c
  * @brief   Interrupt handlers for the test app.
  *
  *   The bootloader leaves the core with PRIMASK set (global IRQs off) when it
  *   jumps here, so the app must re-enable interrupts itself (see main.c). Once
  *   IRQs are on, the SysTick interrupt fires every 1 ms and must advance the
  *   HAL tick; without this, HAL_Delay() blocks forever.
  ******************************************************************************
  */
#include "stm32h7xx_hal.h"
#include "bsp_log.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* USART1 TXE interrupt -> drains the bsp_log TX ring buffer (non-blocking
   PRINT_LOG). Without it the 1 Hz heartbeat output would stall once the ring
   buffer filled up. */
void USART1_IRQHandler(void)
{
    log_uart_tx_irq();
}
