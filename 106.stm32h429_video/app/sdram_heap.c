/**
 * @file    sdram_heap.c
 * @brief   FreeRTOS kernel heap (ucHeap) placed in external SDRAM.
 * @note    为什么是 SDRAM 而不是内部 SRAM: document/stm32f429/video_player_port.md A1
 */
#include "FreeRTOS.h"

/* 512 KB heap living in external SDRAM (Bank1, 0xC0000000).
 *
 * The section name is compiler dependent because ARM Compiler 6 decides
 * RW vs ZI purely from the name: only a .bss.* name is emitted as zero-init.
 * With a plain custom name (what GCC needs, see the .ld NOLOAD section) armclang
 * emits RW data and armlink copies 512 KB of zeros from FLASH into the UNINIT
 * region during startup -- writing to 0xC0000000 before FMC is up, i.e. a
 * HardFault.  #pragma arm section is no help: it is an AC5-only extension.
 */
#if defined(__ARMCC_VERSION)
uint8_t ucHeap[configTOTAL_HEAP_SIZE]
    __attribute__((section(".bss.freertos_heap"), aligned(32)));
#else
uint8_t ucHeap[configTOTAL_HEAP_SIZE]
    __attribute__((section(".freertos_heap"), aligned(32)));
#endif

/* Single-region heap description consumed by vPortDefineHeapRegions(). */
HeapRegion_t xHeapRegions[] =
    {
        {ucHeap, sizeof(ucHeap)},
        {NULL, 0}};
