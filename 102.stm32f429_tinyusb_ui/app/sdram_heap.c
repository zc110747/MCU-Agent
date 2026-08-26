/**
  ******************************************************************************
  * @file    sdram_heap.c
  * @brief   FreeRTOS kernel heap (ucHeap) placed in external SDRAM.
  *
  *          Used as the single heap region for heap_5.  The .freertos_heap
  *          section is NOLOAD in the linker script, so the startup code does
  *          NOT zero it (accessing 0xC0000000 before FMC/SDRAM is up would
  *          HardFault).  heap_5 initializes it lazily on the first
  *          pvPortMalloc, after vPortDefineHeapRegions() is called from main()
  *          once SDRAM is confirmed working.
  *
  *          IMPORTANT: main() calls bsp_sdram_init() BEFORE vPortDefineHeapRegions()
  *          and long before any xTaskCreate / pvPortMalloc.  Every FreeRTOS
  *          object is allocated from this region, so defining it before SDRAM
  *          is up would write control blocks into uninitialized memory and
  *          corrupt the heap free list.  Keep SDRAM bring-up first.
  ******************************************************************************
  */
#include "FreeRTOS.h"

/* 512 KB heap living in external SDRAM (Bank1, 0xC0000000). */
uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".freertos_heap"), aligned(32)));

/* Single-region heap description consumed by vPortDefineHeapRegions(). */
HeapRegion_t xHeapRegions[] =
{
  { ucHeap, sizeof(ucHeap) },
  { NULL, 0 }
};
