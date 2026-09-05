/* ---------------------------------------------------------------------------
 * STM32H743ZIT6 - 2 MB Flash / 1 MB RAM
 *
 * Code + read-only data live in FLASH (0x08000000).
 * .data/.bss/heap/stack live in DTCMRAM (0x20000000, 128 KB) - zero-wait-state
 * tightly-coupled memory, fastest option for the CPU.
 *
 * IMPORTANT - bus-master reachability:
 *   DTCMRAM (0x20000000) is NOT reachable by DMA1/DMA2, so every DMA buffer
 *   must live in AXI SRAM. The .dma_buf section below is placed in RAM_D1
 *   (0x24000000) for that reason. It is NOLOAD: the C startup never touches
 *   it, therefore none of its cache lines is dirty when the first DMA starts
 *   and a plain Invalidate can never drop data.
 *
 *   The DWC2 USB core runs in FIFO (slave) mode, so its buffers may stay in
 *   DTCM. If you enable CFG_TUD_DWC2_DMA_ENABLE, move them to .ram_d1.
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
_Min_Heap_Size  = 0x800;   /* 2 KB  */
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
    . = ALIGN(4);
    *(.text*)
    . = ALIGN(4);
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

  /* ---------------------------------------------------------------------
   * DMA buffers - AXI SRAM, NOLOAD.
   *
   * Used through __attribute__((section(".dma_buf"), aligned(32))).
   *   - 0x24000000 is reachable by DMA1/DMA2
   *   - it is cached (write-back, write-allocate): every CPU access is
   *     bracketed by SCB_CleanDCache_by_Addr (TX) or
   *     SCB_InvalidateDCache_by_Addr (RX)
   *   - the section boundaries are 32-byte aligned and every object in it
   *     is a multiple of 32 bytes, so no cache line is ever shared with
   *     foreign data and a by-address cache operation can never touch a
   *     line owned by somebody else
   * -------------------------------------------------------------------*/
  .dma_buf (NOLOAD) :
  {
    . = ALIGN(32);
    __sdma_buf = .;
    *(.dma_buf)
    *(.dma_buf*)
    . = ALIGN(32);
    __edma_buf = .;
  } >RAM_D1

  /* Optional general purpose AXI SRAM scratch:
   * __attribute__((section(".ram_d1"))) */
  .ram_d1 (NOLOAD) :
  {
    . = ALIGN(32);
    _sram_d1 = .;
    *(.ram_d1)
    *(.ram_d1*)
    . = ALIGN(32);
    _eram_d1 = .;
  } >RAM_D1

  /* D2 SRAM (288 KB) - reachable by DMA1/DMA2 as well */
  .ram_d2 (NOLOAD) :
  {
    . = ALIGN(32);
    _sram_d2 = .;
    *(.ram_d2)
    *(.ram_d2*)
    . = ALIGN(32);
    _eram_d2 = .;
  } >RAM_D2

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
