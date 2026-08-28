/*
 * Linker script for STM32H743ZIT6 BOOTLOADER
 *
 * Flash layout (2 MB, 16 sectors x 128 KB):
 *   0x08000000 - 0x0801FFFF : bootloader (this image, 128 KB = sector 0)
 *   0x08020000 - 0x081DFFFF : app        (14 sectors = sectors 1..14)
 *   0x081E0000 - 0x081FFFFF : system config sector (sector 15)
 *
 * RAM:
 *   DTCM  0x20000000, 128 KB : stack (top)
 *   SRAM1 0x24000000, 512 KB : .data / .bss / heap (AXI SRAM) + upgrade engine
 *
 * The upgrade engine (.upgrade_ram) is LOADed in FLASH but RUNs from AXI SRAM
 * (0x24000000): while erasing/programming bank1 the CPU must not fetch
 * instructions from bank1, so all flash write code executes from a RAM region
 * that is independent of both flash banks AND executable. NOTE: the code must
 * NOT run from DTCM - on Cortex-M7 the I-Code bus cannot fetch instructions
 * from DTCM (data-TCM only), which would BusFault on the first engine call.
 */

ENTRY(Reset_Handler)

_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x400;

MEMORY
{
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 128K    /* bootloader only */
  DTCM    (rwx) : ORIGIN = 0x20000000, LENGTH = 128K    /* stack + upgrade engine */
  SRAM1   (rwx) : ORIGIN = 0x24000000, LENGTH = 512K    /* data/bss (AXI SRAM) */
}

/* Initial stack pointer: top of DTCM RAM */
_estack = ORIGIN(DTCM) + LENGTH(DTCM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(8);
    KEEP(*(.isr_vector))
    . = ALIGN(8);
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
    _etext = .;
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

  /*
   * Flash upgrade engine: linked into AXI SRAM (0x24000000), loaded from FLASH.
   * BFLASH_Relocate() copies _supgrade_ram_load..+size to _supgrade_ram before
   * the first use. All symbols in this section resolve to the AXI SRAM run
   * address (executable, independent of both flash banks). Do NOT place this in
   * DTCM: Cortex-M7 cannot fetch instructions from DTCM.
   */
  .upgrade_ram :
  {
    . = ALIGN(8);
    _supgrade_ram = .;
    *(.upgrade_ram)
    *(.upgrade_ram*)
    . = ALIGN(8);
    _eupgrade_ram = .;
  } >SRAM1 AT> FLASH
  _supgrade_ram_load = LOADADDR(.upgrade_ram);

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
