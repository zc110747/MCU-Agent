/**
  ******************************************************************************
  * @file    sdram_heap.c
  * @brief   FreeRTOS kernel heap (ucHeap) placed in external SDRAM.
  *
  *          Enabled by configAPPLICATION_ALLOCATED_HEAP=1 in FreeRTOSConfig.h.
  *          The .freertos_heap section is NOLOAD in the linker script, so the
  *          startup code does NOT zero it (SDRAM is not accessible before
  *          bsp_sdram_init()).  heap_4 initializes it lazily on first use,
  *          which happens after main() has brought up the FMC/SDRAM.
  ******************************************************************************
  */
#include "FreeRTOS.h"

uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".freertos_heap"), aligned(8)));
