/*
******************************************************************************
**  Linker script for STM32H743ZIT6
**    FLASH : 2 MB  @ 0x08000000
**    DTCM  : 128 KB @ 0x20000000  (code data / stack / heap, CPU only, no DMA)
**    AXI   : 512 KB @ 0x24000000  (camera frame buffers, DMA reachable)
**    SRAM_D2 : 288 KB @ 0x30000000
**    SRAM_D3 :  64 KB @ 0x38000000
******************************************************************************
*/

ENTRY(Reset_Handler)

/* Top of the DTCM: main stack grows downwards from here */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);

_Min_Heap_Size  = 0x400;  /* 1 KB heap  */
_Min_Stack_Size = 0x1000; /* 4 KB stack */

MEMORY
{
  ITCMRAM (xrw) : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1  (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
  RAM_D2  (xrw) : ORIGIN = 0x30000000, LENGTH = 288K
  RAM_D3  (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
}

SECTIONS
{
  /* Interrupt vector table */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  /* Program code */
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

  /* Read-only data */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
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

  /* Initialised data: loaded from FLASH into DTCM at startup */
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

  /* Zero-initialised data */
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

  /* Heap / stack guard */
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
   * Camera frame buffers in AXI SRAM (D1 domain) at 0x24000000.
   * DMA1/DMA2 can reach this region; the MPU marks it non-cacheable so
   * that DCMI DMA writes are immediately visible to the CPU.
   * ------------------------------------------------------------------ */
  .framebuffer (NOLOAD) :
  {
    . = ALIGN(32);
    _sframebuf = .;
    *(.framebuffer)
    *(.framebuffer*)
    . = ALIGN(32);
    _eframebuf = .;
  } >RAM_D1

  /* Generic AXI SRAM section for anything else placed in D1 */
  .ram_d1 (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d1)
    *(.ram_d1*)
    . = ALIGN(32);
  } >RAM_D1

  /* SRAM in D2 domain (reachable by all DMAs) */
  .ram_d2 (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d2)
    *(.ram_d2*)
    . = ALIGN(32);
  } >RAM_D2

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
