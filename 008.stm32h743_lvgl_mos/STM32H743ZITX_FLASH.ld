/*
******************************************************************************
**  File        : STM32H743ZITX_FLASH.ld
**  Target      : STM32H743ZIT6 (2MB Flash / 1MB RAM)
**  Toolchain   : GNU arm-none-eabi-gcc
**
**  Memory notes
**  ------------
**  The main RAM region is deliberately mapped to AXI-SRAM (D1, 0x24000000)
**  instead of DTCM (0x20000000):  the SDMMC1 internal DMA (IDMA) cannot
**  access DTCM, so any FatFs / SD sector buffer living there would silently
**  fail.  AXI-SRAM is reachable by every master, so .data/.bss/heap/stack all
**  go there and SD transfers "just work".
**
**  DTCM is still exported as a separate region for latency critical data.
******************************************************************************
*/

ENTRY(Reset_Handler)

/* Highest address of the user mode stack (end of AXI SRAM) */
_estack = ORIGIN(RAM_D1) + LENGTH(RAM_D1);    /* 0x24080000 */

_Min_Heap_Size  = 0x2000;   /* 8 KB heap  */
_Min_Stack_Size = 0x2000;   /* 8 KB stack */

MEMORY
{
  ITCMRAM (xrw) : ORIGIN = 0x00000000, LENGTH = 64K
  DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1  (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
  RAM_D2  (xrw) : ORIGIN = 0x30000000, LENGTH = 288K
  RAM_D3  (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
}

SECTIONS
{
  /* Interrupt vector table -> start of flash */
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

  /* Constant data */
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

  /* Load address of .data in flash */
  _sidata = LOADADDR(.data);

  /* Initialised data -> AXI SRAM, loaded from flash by Reset_Handler */
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

  /* Heap / stack guard: fails the link if RAM is exhausted */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM_D1

  /* Optional: objects explicitly placed in other RAM domains via
     __attribute__((section(".dtcm"))) / (".ram_d2")               */
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

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
