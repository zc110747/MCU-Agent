/*
******************************************************************************
**  Linker script for STM32H743ZIT6 (2 MB flash, 1 MB RAM), GCC / newlib.
**
**  Memory layout used by this project
**    FLASH   0x08000000  2048K   code + rodata
**    RAM_D1  0x24000000   512K   .data/.bss/heap/stack  <-- main working RAM
**                                (holds the 112.5 KB frame buffer)
**    DTCMRAM 0x20000000   128K   free for fast scratch (.dtcm section)
**    RAM_D2  0x30000000   288K   free (.ram_d2 section)
**    RAM_D3  0x38000000    64K   free (.ram_d3 section)
**    ITCMRAM 0x00000000    64K   free (.itcm section)
******************************************************************************
*/

ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM_D1) + LENGTH(RAM_D1);   /* end of AXI SRAM */

_Min_Heap_Size  = 0x1000;   /*  4 KB heap  */
_Min_Stack_Size = 0x4000;   /* 16 KB stack */

MEMORY
{
  ITCMRAM (xrw)  : ORIGIN = 0x00000000, LENGTH = 64K
  DTCMRAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1  (xrw)  : ORIGIN = 0x24000000, LENGTH = 512K
  RAM_D2  (xrw)  : ORIGIN = 0x30000000, LENGTH = 288K
  RAM_D3  (xrw)  : ORIGIN = 0x38000000, LENGTH = 64K
  FLASH   (rx)   : ORIGIN = 0x08000000, LENGTH = 2048K
}

SECTIONS
{
  /* Vector table goes first in flash */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  /* Program code and other data */
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

  .ARM.extab : {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM : {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  /* Used by the startup code to initialise .data */
  _sidata = LOADADDR(.data);

  /* Initialised data: loaded from flash into RAM_D1 at reset */
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
  } >RAM_D1 AT> FLASH

  /* Zero initialised data */
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
  } >RAM_D1

  /* Make sure heap + stack still fit in RAM_D1 */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM_D1

  /* --- optional placement helpers, unused sections cost nothing --------- */
  .dtcm (NOLOAD) :
  {
    . = ALIGN(4);
    *(.dtcm)
    *(.dtcm*)
    . = ALIGN(4);
  } >DTCMRAM

  .ram_d2 (NOLOAD) :
  {
    . = ALIGN(4);
    *(.ram_d2)
    *(.ram_d2*)
    . = ALIGN(4);
  } >RAM_D2

  .ram_d3 (NOLOAD) :
  {
    . = ALIGN(4);
    *(.ram_d3)
    *(.ram_d3*)
    . = ALIGN(4);
  } >RAM_D3

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
