/*
******************************************************************************
**  File        : STM32H743ZITX_FLASH.ld
**  Target      : STM32H743ZIT6 (2MB Flash / 1MB RAM)
**  Toolchain   : GNU arm-none-eabi-gcc
**
**  Memory notes
**  ------------
**  The main RAM region is AXI-SRAM (D1, 0x24000000):  .data/.bss/heap/stack
**  and the SD sector buffers all live here.  DTCM (0x20000000, 128 kB) and
**  RAM_D2 (0x30000000, 288 kB) are NOT placed by the linker - they are carved
**  at runtime by bsp/sram_pool.c and handed out only while an app needs them
**  (the NES emulator takes the machine state in DTCM and the ROM image in D2;
**  both are freed on close so a camera or similar can use them otherwise).
**  With D-Cache enabled (see main.c MPU_Config) the AXI-SRAM and RAM_D2 banks
**  are write-back cache regions; RAM_D3 (SRAM4, 0x38000000) holds the USB DMA
**  buffers and is mapped NON-CACHEABLE by MPU Region 2.
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

  /* DTCM (0x20000000) and RAM_D2 (0x30000000) are managed at runtime by
     bsp/sram_pool.c and must NOT appear here, otherwise the linker would
     reserve them as empty NOLOAD sections.  See the "Memory notes" above. */

  /* USB / tinyusb DMA buffers.  The OTG internal DMA writes here directly,
     so this block is mapped NON-CACHEABLE by MPU Region 2 in main.c.  It must
     NOT sit in a write-back cached region or the CPU and DMA disagree on the
     buffer contents and the USB stack eventually corrupts itself (the
     "USB dies after some traffic" failure).  SRAM4 (D3) is spare, so it goes
     here.  See bsp/tusb_config.h (CFG_TUD_MEM_SECTION). */
  .usb_ram (NOLOAD) :
  {
    . = ALIGN(4);
    *(.usb_ram)
    *(.usb_ram*)
    . = ALIGN(4);
  } >RAM_D3

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
