/**
  ******************************************************************************
  * @file    Bsp/emWin/GUIConf.c
  * @brief   emWin memory pool + default font assignment.
  *
  *  The pool is a static array, so it lands in .bss which the linker script
  *  places in AXI-SRAM (RAM_D1, 0x24000000).  128 KB is ample for this static
  *  info panel (no window manager, no memory devices).
  ******************************************************************************
  */
#include "GUI.h"
#include "emwin_font_gbk.h"

#define GUI_NUMBYTES  (0x20000)   /* 128 KB */

/*********************************************************************
*
*       GUI_X_Config
*
*  Called by GUI_Init() to hand emWin its dynamic memory and to pick the
*  default font.
*/
void GUI_X_Config(void) {
  static U32 aMemory[GUI_NUMBYTES / 4] __attribute__((aligned(32)));

  GUI_ALLOC_AssignMemory(aMemory, (unsigned)sizeof(aMemory));
  GUI_SetDefaultFont(&EMWIN_FONT_GBK16);
}
