/**
 ******************************************************************************
 * @file    sdram_heap.c
 * @brief   FreeRTOS kernel heap (ucHeap) placed in external SDRAM.
 *
 *          Used as the single heap region for heap_5.  The heap section is
 *          NOLOAD in the linker script, so the startup code does NOT zero it
 *          (accessing 0xC0000000 before FMC/SDRAM is up would HardFault).
 *          heap_5 initializes it lazily on the first pvPortMalloc, after
 *          vPortDefineHeapRegions() is called from main()
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

/* ---- placement attribute -------------------------------------------------
 * Two names are needed because the toolchains disagree on how a custom
 * section can be made ZI (SHT_NOBITS, i.e. no contents in the image):
 *
 *   GCC      : an uninitialised array in ANY custom section already becomes
 *              SHT_NOBITS, so ".freertos_heap" works.  It must NOT start with
 *              ".bss": the linker script's .bss output section matches
 *              *(.bss*) and would swallow the heap before the NOLOAD SDRAM
 *              region ever sees it (measured: RAM overflowed by 406504 bytes).
 *   ARMCLANG : a plain custom name becomes PROGBITS with 512 KB of zeros;
 *              armlink then gives it a load address inside LR_IROM1 and
 *              __main's scatter-load copies it down to 0xC0000000 -- before
 *              main() brings the FMC/SDRAM controller up, i.e. HardFault
 *              before the first application instruction.  `UNINIT` only
 *              suppresses the zero-fill of a ZI region, it does not stop a
 *              PROGBITS copy, and armclang 6.14 has no AC5 zero_init.  A name
 *              starting with ".bss" is what makes armclang emit NOBITS.
 *
 * The linker script / scatter file select both names, so each toolchain
 * matches exactly the section it actually produced.
 * ------------------------------------------------------------------------ */
#if defined(__ARMCC_VERSION)
#define SDRAM_HEAP_SECTION ".bss.freertos_heap"
#else
#define SDRAM_HEAP_SECTION ".freertos_heap"
#endif

/* 512 KB heap living in external SDRAM (Bank1, 0xC0000000). */
uint8_t ucHeap[configTOTAL_HEAP_SIZE]
    __attribute__((section(SDRAM_HEAP_SECTION), aligned(32)));

/* Single-region heap description consumed by vPortDefineHeapRegions(). */
HeapRegion_t xHeapRegions[] =
    {
        {ucHeap, sizeof(ucHeap)},
        {NULL, 0}};
