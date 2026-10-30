/**
  ******************************************************************************
  * @file    lv_port_indev.h
  * @brief   LVGL input device port for the GT9147 capacitive panel.
  ******************************************************************************
  */
#ifndef __LV_PORT_INDEV_H__
#define __LV_PORT_INDEV_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Register the touch panel as a LVGL pointer input device.
  *         Call once, after lv_init() and after the display is registered.
  */
void lv_port_indev_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __LV_PORT_INDEV_H__ */
