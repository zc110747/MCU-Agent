/* ---------------------------------------------------------------------------
 * Core / system interrupt handlers.
 *
 * The bridge's own interrupts (UART4, DMA1_Stream0/1, EXTI0) live next to the
 * driver they belong to, in app/uart_bridge.c.
 * -------------------------------------------------------------------------*/

#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "bsp.h"
#include "tusb.h"

void NMI_Handler(void)            { while (1) { } }
void HardFault_Handler(void)      { while (1) { } }
void MemManage_Handler(void)      { while (1) { } }
void BusFault_Handler(void)       { while (1) { } }
void UsageFault_Handler(void)     { while (1) { } }
void SVC_Handler(void)            { }
void DebugMon_Handler(void)       { }
void PendSV_Handler(void)         { }

void SysTick_Handler(void) {
  HAL_IncTick();
}

/* USB2_OTG_FS on PA11/PA12 - TinyUSB rhport 0 */
void OTG_FS_IRQHandler(void) {
  tud_int_handler(0);
}

/* Default_Handler is provided by the startup file (weak aliases). */
