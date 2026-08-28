/**
  ******************************************************************************
  * @file    bsp_touch.c
  * @brief   Touch service: GT9147 + T_PEN EXTI + binary semaphore + mapping.
  ******************************************************************************
  */
#include "bsp_touch.h"
#include "bsp_gt9147.h"
#include "bsp_lcd.h"
#include "semphr.h"
#include <stdio.h>

EXTI_HandleTypeDef g_touch_exti;

static SemaphoreHandle_t s_touch_sem = NULL;

/* Published state, shared between touch_task (writer) and the LVGL indev
 * reader running inside ui_task.  Updated under a critical section so the
 * reader never sees a half-written coordinate pair. */
static volatile uint16_t s_x       = 0U;
static volatile uint16_t s_y       = 0U;
static volatile uint8_t  s_pressed = 0U;

static volatile uint32_t s_irq_count   = 0U;
static volatile uint32_t s_press_count = 0U;

/* LVGL canvas (= the active GRAM window) and the touch resolution the chip
 * reports for itself.  Refreshed on every scan because the panel is brought up
 * by another thread (ui_task) and the window is only final afterwards. */
static uint16_t s_max_x   = 1U;
static uint16_t s_max_y   = 1U;
static uint16_t s_panel_x = 0U;
static uint16_t s_panel_y = 0U;

/**
  * @brief  Re-read the mapping targets from the LCD driver and the chip.
  */
static void touch_refresh_geometry(void)
{
    LCD_INFO *info = get_lcd_info();

    s_panel_x = bsp_gt9147_panel_x();
    s_panel_y = bsp_gt9147_panel_y();

    if (lcd_driver_ready() != 0)
    {
        s_max_x = (uint16_t)((info != NULL) ? info->lcd_width  : 1U);
        s_max_y = (uint16_t)((info != NULL) ? info->lcd_height : 1U);
        if (s_max_x == 0U) { s_max_x = 1U; }
        if (s_max_y == 0U) { s_max_y = 1U; }
    }
}

/* -------------------------------------------------------------------------- */
/* Interrupt side                                                             */
/* -------------------------------------------------------------------------- */

/**
  * @brief  T_PEN falling edge.  Runs in ISR context: only the semaphore is
  *         touched here, the I2C transfer is left to the task.
  */
static void touch_exti_cb(void)
{
    BaseType_t higher_prio_task_woken = pdFALSE;

    s_irq_count++;

    if (s_touch_sem != NULL)
    {
        xSemaphoreGiveFromISR(s_touch_sem, &higher_prio_task_woken);
        portYIELD_FROM_ISR(higher_prio_task_woken);
    }
}

static int touch_exti_init(void)
{
    EXTI_ConfigTypeDef cfg;

    (void)HAL_EXTI_GetHandle(&g_touch_exti, EXTI_LINE_7);

    cfg.Line    = EXTI_LINE_7;
    cfg.Mode    = EXTI_MODE_INTERRUPT;
    cfg.Trigger = EXTI_TRIGGER_FALLING;
    cfg.GPIOSel = EXTI_GPIOH;               /* T_PEN is PH7 */

    if (HAL_EXTI_SetConfigLine(&g_touch_exti, &cfg) != HAL_OK)
    {
        return -1;
    }

    if (HAL_EXTI_RegisterCallback(&g_touch_exti, HAL_EXTI_COMMON_CB_ID,
                                  touch_exti_cb) != HAL_OK)
    {
        return -1;
    }

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, TOUCH_EXTI_PREEMPT_PRIO, 0U);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    /* Register-level echo: if the interrupt never fires this is the one line
     * that shows whether the line is muxed to GPIOH at all. */
    printf("[TOUCH] EXTI cfg: EXTICR2=0x%08lX IMR=0x%04lX FTSR=0x%04lX prio=%u\r\n",
           (unsigned long)SYSCFG->EXTICR[1], (unsigned long)EXTI->IMR,
           (unsigned long)EXTI->FTSR, (unsigned int)TOUCH_EXTI_PREEMPT_PRIO);

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Coordinate mapping                                                         */
/* -------------------------------------------------------------------------- */
static void map_point(uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y)
{
    uint32_t x = raw_x;
    uint32_t y = raw_y;

#if TOUCH_SWAP_XY
    { uint32_t t = x; x = y; y = t; }
#endif

    /* Scale the chip's own touch resolution onto the display canvas.  When
     * the two match (the usual case) this is the identity. */
    if (s_panel_x > 1U) { x = (x * (s_max_x - 1U)) / (s_panel_x - 1U); }
    if (s_panel_y > 1U) { y = (y * (s_max_y - 1U)) / (s_panel_y - 1U); }

#if TOUCH_INVERT_X
    x = (uint32_t)(s_max_x - 1U) - x;
#endif
#if TOUCH_INVERT_Y
    y = (uint32_t)(s_max_y - 1U) - y;
#endif

    if (x >= s_max_x) { x = (s_max_x > 0U) ? (s_max_x - 1U) : 0U; }
    if (y >= s_max_y) { y = (s_max_y > 0U) ? (s_max_y - 1U) : 0U; }

    *out_x = (uint16_t)x;
    *out_y = (uint16_t)y;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
int bsp_touch_init(void)
{
    int ret;

    /* The mapping target is the GRAM window the LVGL display is registered
     * with.  It is only final once ui_task has brought the panel up, so wait
     * for that instead of silently clamping every coordinate to 0. */
    {
        uint32_t waited = 0U;

        while (lcd_driver_ready() == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(50U));
            waited += 50U;
            if (waited >= 10000U)
            {
                printf("[TOUCH] LCD never came up, mapping to 1x1\r\n");
                return -1;
            }
        }
    }

    s_max_x = 1U;
    s_max_y = 1U;

    if (s_touch_sem == NULL)
    {
        s_touch_sem = xSemaphoreCreateBinary();
    }
    if (s_touch_sem == NULL)
    {
        printf("[TOUCH] semaphore create FAILED\r\n");
        return -1;
    }
    (void)xSemaphoreTake(s_touch_sem, 0U);      /* start empty */

    ret = bsp_gt9147_init();
    if (ret != 0)
    {
        printf("[TOUCH] controller init FAILED (%d)\r\n", ret);
        return -1;
    }

    if (touch_exti_init() != 0)
    {
        printf("[TOUCH] EXTI init FAILED\r\n");
        return -1;
    }

    touch_refresh_geometry();

    printf("[TOUCH] ready: id=%s addr=0x%02X cfg=0x%02X, touch %ux%u -> canvas %ux%u, "
           "swap=%d invX=%d invY=%d\r\n",
           bsp_gt9147_id(), (unsigned int)bsp_gt9147_addr(),
           (unsigned int)bsp_gt9147_cfg_version(),
           (unsigned int)s_panel_x, (unsigned int)s_panel_y,
           (unsigned int)s_max_x, (unsigned int)s_max_y,
           (int)TOUCH_SWAP_XY, (int)TOUCH_INVERT_X, (int)TOUCH_INVERT_Y);

    return 0;
}

int bsp_touch_is_ready(void)
{
    return (bsp_gt9147_is_ready() != 0) ? 1 : 0;
}

int bsp_touch_wait(TickType_t ticks)
{
    if (s_touch_sem == NULL)
    {
        return 0;
    }
    return (xSemaphoreTake(s_touch_sem, ticks) == pdTRUE) ? 1 : 0;
}

int bsp_touch_scan(void)
{
    gt9147_point_t pts[GT9147_MAX_POINTS];
    uint16_t mx = 0U;
    uint16_t my = 0U;
    int n;

    if (bsp_gt9147_is_ready() == 0)
    {
        return -1;
    }

    /* Pick up the canvas size (and the chip's own resolution) fresh: ui_task
     * may have finished bringing the panel up after this task started. */
    touch_refresh_geometry();

    n = bsp_gt9147_read(pts, GT9147_MAX_POINTS);
    if (n < 0)
    {
        return -1;
    }

    if (n > 0)
    {
        map_point(pts[0].x, pts[0].y, &mx, &my);
        s_press_count++;
    }

    taskENTER_CRITICAL();
    s_x       = mx;
    s_y       = my;
    s_pressed = (n > 0) ? 1U : 0U;
    taskEXIT_CRITICAL();

    if (n > 0)
    {
        /* Raw coordinates are printed as well: they are what the mapping
         * macros below act on, so a single touch is enough to pick the right
         * combination if a panel is mounted differently.  The IRQ counter
         * shows whether the T_PEN interrupt reached us - if it stays at 0
         * while contacts are detected, the touch is only working because of
         * the task's idle poll. */
        printf("[TOUCH] raw=(%u,%u) -> lv=(%u,%u) points=%d irq=%lu\r\n",
               (unsigned int)pts[0].x, (unsigned int)pts[0].y,
               (unsigned int)mx, (unsigned int)my, n,
               (unsigned long)s_irq_count);
    }

    return n;
}

void bsp_touch_get(uint16_t *x, uint16_t *y, uint8_t *pressed)
{
    if (x != NULL)       { *x = s_x; }
    if (y != NULL)       { *y = s_y; }
    if (pressed != NULL) { *pressed = s_pressed; }
}

uint32_t bsp_touch_irq_count(void)
{
    return s_irq_count;
}

uint32_t bsp_touch_press_count(void)
{
    return s_press_count;
}
