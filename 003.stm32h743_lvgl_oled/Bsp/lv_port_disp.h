/**
  ******************************************************************************
  * @file    lv_port_disp.h
  * @brief   LVGL display port for the ST7789 240x240 panel on SPI6.
  ******************************************************************************
  */
#ifndef __LV_PORT_DISP_H
#define __LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Register the ST7789 panel as LVGL's display.
  * @note   Call after driver_spi_oled_init() and after lv_init().
  */
void lv_port_disp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __LV_PORT_DISP_H */
