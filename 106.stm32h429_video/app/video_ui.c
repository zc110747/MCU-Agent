/**
 * @file    video_ui.c
 * @brief   LVGL video screen: full-panel image + two text bands.
 * @note    全屏 TRUE_COLOR image: document/stm32f429/video_player_port.md A2
 */
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "lvgl.h"

#include "video_ui.h"
#include "bsp_lcd.h"
#include "lv_font_gbk.h"
#include "log.h"

typedef struct
{
    uint16_t         *surface;
    uint16_t          width;
    uint16_t          height;
    lv_obj_t         *screen;
    lv_obj_t         *image;
    lv_obj_t         *title;
    lv_obj_t         *status;
    lv_img_dsc_t      image_dsc;
    SemaphoreHandle_t mutex;
    uint8_t           ready;
} video_ui_state_t;

static video_ui_state_t s_ui = {0};

void video_ui_lock(void)
{
    if (s_ui.mutex != NULL)
    {
        (void)xSemaphoreTakeRecursive(s_ui.mutex, portMAX_DELAY);
    }
}

void video_ui_unlock(void)
{
    if (s_ui.mutex != NULL)
    {
        (void)xSemaphoreGiveRecursive(s_ui.mutex);
    }
}

uint8_t video_ui_ready(void)
{
    return s_ui.ready;
}

uint16_t *video_ui_surface(void)
{
    return s_ui.surface;
}

void video_ui_surface_size(uint16_t *width, uint16_t *height)
{
    if (width != NULL)
    {
        *width = s_ui.width;
    }
    if (height != NULL)
    {
        *height = s_ui.height;
    }
}

/** One-line label with a solid dark background, so text stays readable. */
static lv_obj_t *make_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                            const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(label, 2, 0);
    return label;
}

int8_t video_ui_init(void)
{
    if (s_ui.ready != 0U)
    {
        return 0;
    }

    if (s_ui.mutex == NULL)
    {
        s_ui.mutex = xSemaphoreCreateRecursiveMutex();
        if (s_ui.mutex == NULL)
        {
            PRINT_LOG("[UI  ] mutex alloc FAILED\r\n");
            return 1;
        }
    }

    s_ui.width   = (uint16_t)LCD_Width;
    s_ui.height  = (uint16_t)LCD_Height;
    s_ui.surface = (uint16_t *)pvPortMalloc((size_t)s_ui.width *
                                            (size_t)s_ui.height *
                                            sizeof(uint16_t));
    if (s_ui.surface == NULL)
    {
        PRINT_LOG("[UI  ] video surface alloc FAILED (SDRAM heap)\r\n");
        return 1;
    }
    memset(s_ui.surface, 0, (size_t)s_ui.width * (size_t)s_ui.height * sizeof(uint16_t));

    video_ui_lock();

    s_ui.screen = lv_scr_act();
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, 0);

    s_ui.image_dsc.header.always_zero = 0;
    s_ui.image_dsc.header.w           = s_ui.width;
    s_ui.image_dsc.header.h           = s_ui.height;
    s_ui.image_dsc.header.cf          = LV_IMG_CF_TRUE_COLOR;
    s_ui.image_dsc.data_size          = (uint32_t)s_ui.width * (uint32_t)s_ui.height * 2U;
    s_ui.image_dsc.data               = (const uint8_t *)s_ui.surface;

    s_ui.image = lv_img_create(s_ui.screen);
    lv_img_set_src(s_ui.image, &s_ui.image_dsc);
    lv_obj_set_pos(s_ui.image, 0, 0);

    s_ui.title = make_label(s_ui.screen, 6, 6, &lv_font_gbk_16);
    lv_label_set_text(s_ui.title, "VIDEO");

    s_ui.status = make_label(s_ui.screen, 6, 32, &lv_font_gbk_16);
    lv_label_set_text(s_ui.status, "00:00:00/00:00:00");

    s_ui.ready = 1U;
    lv_timer_handler();

    video_ui_unlock();

    PRINT_LOG("[UI  ] surface %ux%u (%lu KB in SDRAM)\r\n", s_ui.width, s_ui.height,
              (unsigned long)((uint32_t)s_ui.width * s_ui.height * 2U / 1024U));
    return 0;
}

void video_ui_set_title(const char *name, uint16_t index, uint16_t total)
{
    if ((s_ui.ready == 0U) || (name == NULL))
    {
        return;
    }

    video_ui_lock();
    lv_label_set_text_fmt(s_ui.title, "%s  %u/%u", name, (unsigned)index,
                          (unsigned)total);
    video_ui_unlock();
}

void video_ui_set_status(const char *text)
{
    if ((s_ui.ready == 0U) || (text == NULL))
    {
        return;
    }

    video_ui_lock();
    lv_label_set_text(s_ui.status, text);
    video_ui_unlock();
}

void video_ui_frame_ready(void)
{
    if (s_ui.ready == 0U)
    {
        return;
    }

    video_ui_lock();
    lv_obj_invalidate(s_ui.image);
    /* Flush now instead of waiting for the next lv_timer_handler(): the player
     * wants the panel to be up to date before it decodes the next frame. */
    lv_refr_now(NULL);
    video_ui_unlock();
}
