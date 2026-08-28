/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack
 * STM32F429IGT6 system SRAM = SRAM1(112K) + SRAM2(16K) + SRAM3(64K) = 192K,
 * contiguous at 0x20000000..0x2002FFFF.  CCM RAM (64K @ 0x10000000) is NOT
 * contiguous with it and is not reachable by the DMA/ETH controller.
 * Verified on silicon: 0x2002FFFC readable, 0x20030000 bus-faults.
 */
_estack = 0x20030000;    /* end of SRAM (0x20000000 + 192K) */

_Min_Heap_Size = 0x400;  /* required amount of heap */
_Min_Stack_Size = 0x2000; /* 8K: mbedTLS ECDHE/ECDSA (P-256) needs transient stack */

/* Memories definition for STM32F429IGTx (1024K flash / 192K SRAM / 64K CCM) */
MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM   (xrw)     : ORIGIN = 0x20000000, LENGTH = 192K
  CCMRAM (xrw)    : ORIGIN = 0x10000000, LENGTH = 64K
  SDRAM (xrw)     : ORIGIN = 0xC0000000, LENGTH = 32M
}

/* Sections */
SECTIONS
{
  /* The startup code into "FLASH" Rom type memory */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data into "FLASH" Rom type memory */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data into "FLASH" Rom type memory */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab   :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  /* Initialized data sections into RAM, loaded from FLASH */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >RAM AT> FLASH

  /* Uninitialized data section into RAM */
  . = ALIGN(4);
  _sbss = .;           /* define a global symbol at bss start */
  __bss_start__ = _sbss;

  .bss :
  {
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
  } >RAM

  _ebss = .;           /* define a global symbol at bss end */
  __bss_end__ = _ebss;

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* External SDRAM @0xC0000000: LwIP memory pool and FreeRTOS heap.
   * NOLOAD: the startup code must NOT zero/copy these before FMC/SDRAM is
   * initialized in main() (accessing 0xC0000000 earlier would HardFault). */
  .sdram_lwip_pool (NOLOAD) :
  {
    . = ALIGN(8);
    __sdram_lwip_pool_start = .;
    KEEP(*(.lwip_memp_pool))
    . = ALIGN(8);
    __sdram_lwip_pool_end = .;
  } >SDRAM

  .sdram_freertos_heap (NOLOAD) :
  {
    . = ALIGN(32);
    __sdram_freertos_heap_start = .;
    KEEP(*(.freertos_heap))
    . = ALIGN(32);
    __sdram_freertos_heap_end = .;
  } >SDRAM

  .sdram_mbedtls_pool (NOLOAD) :
  {
    . = ALIGN(8);
    __sdram_mbedtls_pool_start = .;
    KEEP(*(.mbedtls_pool))
    . = ALIGN(8);
    __sdram_mbedtls_pool_end = .;
  } >SDRAM

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
