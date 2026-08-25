/*
 * Linker script for the STM32H743 TEST APP
 *
 * The app is booted by the bootloader and runs from internal flash:
 *   0x08020000 - 0x081DFFFF : app (14 sectors = sectors 1..14)
 *   0x08021000              : app version, 4 bytes (major,minor,patch,build)
 *   0x081E0000 - 0x081FFFFF : system config sector (owned by the bootloader)
 *
 * A 4-byte version slot is fixed at 0x08021000 (0x1000 into the image); the
 * bootloader reads it to validate the image and compares it to the version in
 * the system config sector. The vector table sits at 0x08020000.
 */

ENTRY(Reset_Handler)

_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x400;

MEMORY
{
  FLASH   (rx)  : ORIGIN = 0x08020000, LENGTH = 0x001C0000   /* app (sectors 1..14) */
  DTCM    (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
  SRAM1   (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
}

_estack = ORIGIN(DTCM) + LENGTH(DTCM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(8);
    KEEP(*(.isr_vector))
    . = ALIGN(8);
  } >FLASH

  /* Reserve the vector area, then pin the 4-byte version slot at 0x08021000
     (explicit address is more reliable than a location-counter assignment). */
  .app_version 0x08021000 :
  {
    KEEP(*(.app_version))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(8);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(8);
  } >FLASH

  .rodata :
  {
    . = ALIGN(8);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(8);
  } >FLASH

  .ARM.extab :
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

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(8);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(8);
    _edata = .;
  } >SRAM1 AT> FLASH

  .bss :
  {
    . = ALIGN(8);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
    __bss_end__ = _ebss;
  } >SRAM1

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >SRAM1

  /DISCARD/ :
  {
    libc.a(*)
    libm.a(*)
    libgcc.a(*)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
