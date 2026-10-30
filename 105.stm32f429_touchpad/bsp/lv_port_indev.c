/**
  ******************************************************************************
  * @file    lv_port_indev.c
  * @brief   LVGL pointer input device backed by the GT9147 touch controller.
  *
  *  The driver does no bus traffic at all: it only publishes whatever
  *  bsp_touch_scan() (called by touch_task from the T_PEN interrupt) last
  *  stored.  That keeps the I2C transfer out of the LVGL render pump, which
  *  would otherwise stall every redraw for the length of a 400 kHz transfer.
  ******************************************************************************
  */
#include "lv_port_indev.h"
#include "lvgl.h"
#include "bsp_touch.h"

static lv_indev_drv_t s_indev_drv;
static lv_indev_t    *s_indev = NULL;

static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x = 0U;
    uint16_t y = 0U;
    uint8_t  pressed = 0U;

    (void)drv;

    bsp_touch_get(&x, &y, &pressed);

    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
    data->state   = (pressed != 0U) ? LV_INDEV_STATE_PRESSED
                                    : LV_INDEV_STATE_RELEASED;
}

void lv_port_indev_init(void)
{
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touchpad_read;

    s_indev = lv_indev_drv_register(&s_indev_drv);
    (void)s_indev;
}
