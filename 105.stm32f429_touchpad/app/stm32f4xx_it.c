/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  *
  *          SVC/PendSV/SysTick vectors point straight at the FreeRTOS port
  *          handlers (see sys_startup/gcc/startup_stm32f429xx.s), so they are
  *          intentionally not defined here.
  *
  *          TIM7    -> HAL 1 ms time base (SysTick belongs to FreeRTOS)
  *          EXTI9_5 -> GT9147 T_PEN (PH7), see bsp/bsp_touch.c
  ******************************************************************************
  */
#include "stm32f4xx_it.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "tusb.h"

/* TIM7 time base handle (stm32f4xx_hal_timebase_tim.c). */
extern TIM_HandleTypeDef htim7;

/* Touch controller T_PEN (PH7) EXTI handle (bsp/bsp_touch.c). */
extern EXTI_HandleTypeDef g_touch_exti;

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
void NMI_Handler(void)
{
  for (;;) { }
}

void HardFault_Handler(void)
{
  for (;;) { }
}

void MemManage_Handler(void)
{
  for (;;) { }
}

void BusFault_Handler(void)
{
  for (;;) { }
}

void UsageFault_Handler(void)
{
  for (;;) { }
}

void DebugMon_Handler(void)
{
}

/**
  * @brief  TIM7 (dedicated vector) = HAL 1 ms time base.
  */
void TIM7_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim7);
}

/**
  * @brief  EXTI lines 9..5 -> the touch controller's T_PEN (PH7).
  *         HAL_EXTI_IRQHandler clears the pending bit and runs the callback
  *         registered by bsp_touch.c, which gives the touch semaphore (FromISR
  *         variant).  Priority 6 is numerically >= 5
  *         (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY), so that is legal.
  */
void EXTI9_5_IRQHandler(void)
{
  HAL_EXTI_IRQHandler(&g_touch_exti);
}

/**
  * @brief  USB OTG FS -> TinyUSB device stack (port 0).
  *         The DWC2 port keeps the controller in device mode and only needs the
  *         interrupt routed here; priority is set by bsp_usb_hw.c.
  */
void OTG_FS_IRQHandler(void)
{
  tud_int_handler(0);
}
