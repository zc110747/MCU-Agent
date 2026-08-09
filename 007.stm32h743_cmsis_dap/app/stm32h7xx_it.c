/* Cortex-M7 exception handlers and the USB interrupt hand-off to TinyUSB. */

#include "stm32h7xx_hal.h"
#include "tusb.h"

/* ------------------------------------------------------------------------ */
/* Core exceptions                                                           */
/* ------------------------------------------------------------------------ */
void NMI_Handler(void) {
  while (1) { }
}

void HardFault_Handler(void) {
  /* Break here with the debugger: inspect SCB->CFSR / HFSR and the stacked
   * PC to find the offending instruction. */
  while (1) { }
}

void MemManage_Handler(void) {
  while (1) { }
}

void BusFault_Handler(void) {
  while (1) { }
}

void UsageFault_Handler(void) {
  while (1) { }
}

void SVC_Handler(void)      { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void)   { }

void SysTick_Handler(void) {
  HAL_IncTick();
}

/* ------------------------------------------------------------------------ */
/* USB                                                                       */
/*                                                                           */
/* TinyUSB numbers the STM32 controllers consistently across the whole        */
/* family: rhport 0 is always OTG_FS, rhport 1 is always OTG_HS - even though */
/* the H7 reference manual calls them USB2_OTG_FS and USB1_OTG_HS.            */
/* ------------------------------------------------------------------------ */
void OTG_FS_IRQHandler(void) {
  tud_int_handler(0);
}

void OTG_HS_IRQHandler(void) {
  tud_int_handler(1);
}
