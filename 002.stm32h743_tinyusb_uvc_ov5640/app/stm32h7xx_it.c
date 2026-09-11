/**
 ******************************************************************************
 * @file    stm32h7xx_it.c
 * @brief   Interrupt service routines.
 ******************************************************************************
 */

#include "main.h"
#include "bsp_camera.h"
#include "tusb.h"

/* ==========================================================================
 * Fault telemetry
 *
 * There is no UART on this board, so every fault records what happened into
 * plain RAM. Read g_fault_id over SWD ("mdw" in OpenOCD) to find out whether
 * the CPU died and why, instead of guessing from a frozen PC.
 * ========================================================================== */
volatile uint32_t g_fault_id   = 0; /* 0 = no fault, see FAULT_* below */
volatile uint32_t g_fault_cfsr = 0; /* SCB->CFSR  */
volatile uint32_t g_fault_hfsr = 0; /* SCB->HFSR  */
volatile uint32_t g_fault_mmar = 0; /* SCB->MMFAR */
volatile uint32_t g_fault_bfar = 0; /* SCB->BFAR  */

#define FAULT_NMI        1U
#define FAULT_HARD       2U
#define FAULT_MEMMANAGE  3U
#define FAULT_BUS        4U
#define FAULT_USAGE      5U

static void fault_capture(uint32_t id)
{
  g_fault_id   = id;
  g_fault_cfsr = SCB->CFSR;
  g_fault_hfsr = SCB->HFSR;
  g_fault_mmar = SCB->MMFAR;
  g_fault_bfar = SCB->BFAR;

  while (1) {
    __NOP();
  }
}

/* ==========================================================================
 * Cortex-M7 core exceptions
 * ========================================================================== */
void NMI_Handler(void)
{
  fault_capture(FAULT_NMI);
}

void HardFault_Handler(void)
{
  fault_capture(FAULT_HARD);
}

void MemManage_Handler(void)
{
  fault_capture(FAULT_MEMMANAGE);
}

void BusFault_Handler(void)
{
  fault_capture(FAULT_BUS);
}

void UsageFault_Handler(void)
{
  fault_capture(FAULT_USAGE);
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

/* ==========================================================================
 * Peripheral interrupts
 * ========================================================================== */
void DCMI_IRQHandler(void)
{
  HAL_DCMI_IRQHandler(&hdcmi);
}

void DMA2_Stream1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_dcmi);
}

/* USB OTG FS (PA11/PA12) - handled entirely by TinyUSB */
void OTG_FS_IRQHandler(void)
{
  tud_int_handler(0);
}
