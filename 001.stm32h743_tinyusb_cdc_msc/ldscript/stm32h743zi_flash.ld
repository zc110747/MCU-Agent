/* ---------------------------------------------------------------------------
 * STM32H743ZIT6 - 2 MB Flash / 1 MB RAM
 *
 * Code + read-only data live in FLASH (0x08000000).
 * .data/.bss/heap/stack live in DTCMRAM (0x20000000, 128 KB) - zero-wait-state
 * tightly-coupled memory, fastest option for the CPU.
 *
 * NOTE: DTCMRAM is NOT reachable by the USB/DMA bus masters. This project runs
 * the DWC2 USB core in FIFO (slave) mode, so that is fine. If you ever enable
 * CFG_TUD_DWC2_DMA_ENABLE, move the USB buffers into RAM_D1 (AXI SRAM) using
 * the .ram_d1 section provided below.
 * -------------------------------------------------------------------------*/

ENTRY(Reset_Handler)

MEMORY
{
  ITCMRAM (xrw) : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1  (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
  RAM_D2  (xrw) : ORIGIN = 0x30000000, LENGTH = 288K
  RAM_D3  (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
}

/* Stack grows down from the end of DTCMRAM */
_estack     = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);
_Min_Heap_Size  = 0x400;   /* 1 KB  */
_Min_Stack_Size = 0x1000;  /* 4 KB  */

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
    KEEP(*(.init))
    KEEP(*(.fini))
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

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* Initialised data: loaded from FLASH, runs in DTCMRAM */
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

  .bss :
  {
    . = ALIGN(4);
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
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >DTCMRAM

  /* Optional: DMA-capable AXI SRAM. Use __attribute__((section(".ram_d1"))) */
  .ram_d1 (NOLOAD) :
  {
    . = ALIGN(32);
    _sram_d1 = .;
    *(.ram_d1)
    *(.ram_d1*)
    . = ALIGN(32);
    _eram_d1 = .;
  } >RAM_D1

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
