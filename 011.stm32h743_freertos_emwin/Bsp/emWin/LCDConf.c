/**
  ******************************************************************************
  * @file    LCDConf.c
  * @brief   emWin display driver configuration for the write-only ST7789 panel.
  *
  *  The ST7789 is wired to SPI6 as a half-duplex, TX-only (no MISO) RGB565
  *  panel, so there is no way to read pixels back.  We therefore use emWin's
  *  "Lin" (linear framebuffer) driver with a local VRAM that emWin paints
  *  into; the application flushes that VRAM to the panel by streaming it
  *  through OLED_CopyBuffer() (which talks SPI6).  Color conversion is
  *  GUICC_565, whose 16-bit pixel (R in the high byte) matches exactly what
  *  the ST7789 expects.
  ******************************************************************************
  */
#include "GUI.h"
#include "GUIDRV_Lin.h"
#include "LCD.h"
#include "drv_spi_oled.h"

#define XSIZE_PHYS 240
#define YSIZE_PHYS 240

/* Full 240x240 RGB565 framebuffer, living in AXI-SRAM (.bss @ 0x24000000).
 * D-Cache is intentionally OFF in this project, so no cache maintenance is
 * needed around the SPI DMA / direct writes. */
static uint16_t gui_vram[XSIZE_PHYS * YSIZE_PHYS] __attribute__((aligned(32)));

/**
  * @brief  Push the emWin VRAM to the ST7789 panel.
  *         Called by the application once per UI update (the panel holds its
  *         own GRAM, so a 1 Hz flush is enough).
  */
void emwin_flush_vram(void)
{
    OLED_CopyBuffer(0, 0, XSIZE_PHYS, YSIZE_PHYS, gui_vram);
}

/**
  * @brief  Called by GUI_Init() to bind the driver + color conversion and
  *         tell emWin where the framebuffer lives.
  */
void LCD_X_Config(void)
{
    GUI_DEVICE_CreateAndLink(GUIDRV_LIN_16, GUICC_565, 0, 0);

    if (LCD_GetSwapXY())
    {
        LCD_SetSizeEx(0, YSIZE_PHYS, XSIZE_PHYS);
        LCD_SetVSizeEx(0, YSIZE_PHYS, XSIZE_PHYS);
    }
    else
    {
        LCD_SetSizeEx(0, XSIZE_PHYS, YSIZE_PHYS);
        LCD_SetVSizeEx(0, XSIZE_PHYS, YSIZE_PHYS);
    }
    LCD_SetVRAMAddrEx(0, (void *)gui_vram);
}

/**
  * @brief  Low level display driver hook.
  *         LCD_X_INITCONTROLLER is a no-op on purpose: the panel is brought
  *         up by driver_spi_oled_init() inside application_init() *before*
  *         GUI_Init() runs, so emWin must not re-initialise it.
  */
int LCD_X_DisplayDriver(unsigned LayerIndex, unsigned Cmd, void * pData)
{
    (void)LayerIndex;
    (void)pData;

    switch (Cmd)
    {
        case LCD_X_INITCONTROLLER:
            break;
        default:
            return -1;
    }
    return 0;
}
