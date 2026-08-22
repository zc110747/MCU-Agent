/**
  ******************************************************************************
  * @file    sdram_heap.c
  * @brief   FreeRTOS kernel heap (ucHeap) placed in external SDRAM.
  *
  *          Enabled by configAPPLICATION_ALLOCATED_HEAP=1 in FreeRTOSConfig.h.
  *          The .freertos_heap section is NOLOAD in the linker script, so the
  *          startup code does NOT zero it.  Heap_4 initializes it lazily on
  *          first pvPortMalloc.
  *
  *          IMPORTANT: main() calls bsp_sdram_init() BEFORE any FreeRTOS
  *          object is created (xTaskCreate / xSemaphoreCreate*), because every
  *          such object is allocated from ucHeap in SDRAM.  Creating one before
  *          SDRAM is up writes its control block into uninitialized memory and
  *          corrupts the heap_4 free list (heap_4.c:269 assert).  Keep SDRAM
  *          bring-up first.
  ******************************************************************************
  */
#include "FreeRTOS.h"

uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".freertos_heap"), aligned(32)));
