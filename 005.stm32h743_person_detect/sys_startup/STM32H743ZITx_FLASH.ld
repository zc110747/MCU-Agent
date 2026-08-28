/*
 * Linker script for STM32H743ZIT6 (2 MB flash / 1 MB RAM).
 *
 * Memory plan for this project
 * ---------------------------------------------------------------------------
 *  FLASH   0x08000000  2048K  code + const model weights (~257 KB)
 *  DTCMRAM 0x20000000   128K  .data / .bss / heap / stack   (tightly coupled,
 *                             never cached -> no maintenance needed)
 *  RAM_DMA 0x24000000   256K  DCMI double frame buffer (.dma_buffer)
 *                             2 x 192x192x2 = 144 KB, marked NON-CACHEABLE by
 *                             MPU region 1 (regions must be a power of two)
 *  RAM     0x24040000   256K  NN tensor arena, LCD compose buffer (.axi_ram)
 *                             normal write-back cacheable, CPU only
 *  RAM_D2  0x30000000   288K  free
 *  RAM_D3  0x38000000    64K  free
 */

ENTRY(Reset_Handler)

_Min_Heap_Size  = 0x1000;   /* 4 KB heap  */
_Min_Stack_Size = 0x2000;   /* 8 KB stack */

MEMORY
{
  DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_DMA (xrw) : ORIGIN = 0x24000000, LENGTH = 256K
  RAM     (xrw) : ORIGIN = 0x24040000, LENGTH = 256K
  RAM_D2  (xrw) : ORIGIN = 0x30000000, LENGTH = 288K
  RAM_D3  (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
  ITCMRAM (xrw) : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
}

/* Top of the DTCM is the initial stack pointer */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab (READONLY) :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM (READONLY) :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >DTCMRAM AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >DTCMRAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >DTCMRAM

  /* ------------------------------------------------------------------
   * Non-cacheable AXI SRAM window at 0x24000000: DCMI writes here by DMA,
   * so the CPU must never see stale cache lines.  Not initialised at boot.
   * ------------------------------------------------------------------ */
  .dma_buffer (NOLOAD) :
  {
    . = ALIGN(32);
    _sdma_buffer = .;
    *(.dma_buffer)
    *(.dma_buffer*)
    . = ALIGN(32);
    _edma_buffer = .;
  } >RAM_DMA

  /* ------------------------------------------------------------------
   * Cacheable AXI SRAM: neural-network tensor arena and the LCD compose
   * buffer.  CPU-only, so write-back caching is safe and much faster.
   * ------------------------------------------------------------------ */
  .axi_ram (NOLOAD) :
  {
    . = ALIGN(32);
    _saxi_ram = .;
    *(.axi_ram)
    *(.axi_ram*)
    . = ALIGN(32);
    _eaxi_ram = .;
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
