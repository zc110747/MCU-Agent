/**
 * @file    lv_port_disp.h
 * @brief   LVGL display port for the 800x480 NT35510 panel on FMC Bank1 NE1.
 * @note    document/stm32f429/video_player_port.md A2
 */
#ifndef __LV_PORT_DISP_H
#define __LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Register the FMC 8080 panel as LVGL's display.
 * @note   Call after lv_init() and after bsp_lcd_init(); needs the SDRAM heap.
 */
void lv_port_disp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __LV_PORT_DISP_H */
