/* ---------------------------------------------------------------------------
 * stm32h7xx_it.c - core exception handlers and the USB vector.
 *
 * Peripheral vectors live next to the driver that owns them:
 *   UART4_IRQHandler, DMA1_Stream0_IRQHandler  -> bsp/bsp_uart.c
 *   DCMI_IRQHandler, DMA2_Stream1_IRQHandler   -> bsp/bsp_camera.c
 * -------------------------------------------------------------------------*/

#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "tusb.h"

void NMI_Handler(void)        { while (1) { } }
void HardFault_Handler(void)  { while (1) { } }
void MemManage_Handler(void)  { while (1) { } }
void BusFault_Handler(void)   { while (1) { } }
void UsageFault_Handler(void) { while (1) { } }
void SVC_Handler(void)        { }
void DebugMon_Handler(void)   { }
void PendSV_Handler(void)     { }

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* USB2_OTG_FS on PA11/PA12 - TinyUSB rhport 0.
 * TinyUSB must run in thread context (tud_task()); the interrupt only feeds
 * the stack. Nothing blocking is allowed in here. */
void OTG_FS_IRQHandler(void)
{
    tud_int_handler(0);
}

/* Default_Handler is provided by the startup file (weak aliases). */
