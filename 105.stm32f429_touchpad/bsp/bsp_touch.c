/**
  ******************************************************************************
  * @file    bsp_touch.c
  * @brief   Multi-point touch service: GT9147 + T_PEN EXTI + canvas mapping.
  *
  *  Two details in here are carried over verbatim from 102.stm32f429_tinyusb_ui
  *  because both were found the hard way on this very board:
  *
  *   1. The ISR masks EXTI line 7 immediately.  With the FMC/LCD bus running
  *      PH7 picks up tens of kHz of noise from the adjacent bit-banged SCL
  *      (PH6); left unmasked it spent ~10% of the CPU in the ISR.
  *   2. Every bus transaction keeps line 7 masked as well (crosstalk from the
  *      bit-banged clock), and the task re-arms it only after it has stopped
  *      polling.
  ******************************************************************************
  */
#include "bsp_touch.h"
#include "bsp_gt9147.h"
#include "bsp_lcd.h"
#include "semphr.h"
#include <stdio.h>
#include "log.h"

EXTI_HandleTypeDef g_touch_exti;

static SemaphoreHandle_t s_touch_sem = NULL;

static volatile uint32_t s_irq_count = 0U;

/* Latest contact published to LVGL (see bsp_touch_get). */
static volatile uint16_t s_last_x = 0U;
static volatile uint16_t s_last_y = 0U;
static volatile uint8_t  s_last_pressed = 0U;

/* LVGL canvas (= the active GRAM window); refreshed on every scan because the
 * panel is brought up by another thread. */
static uint16_t s_max_x   = 1U;
static uint16_t s_max_y   = 1U;
static uint16_t s_panel_x = 0U;
static uint16_t s_panel_y = 0U;

/* Line 7 (T_PEN = PH7) bit in the EXTI registers. */
#define TOUCH_EXTI_LINE_MSK   (1UL << 7)

/* -------------------------------------------------------------------------- */
/* Interrupt side                                                             */
/* -------------------------------------------------------------------------- */

static void touch_exti_cb(void)
{
    BaseType_t higher_prio_task_woken = pdFALSE;

    s_irq_count++;

    /* Mask straight away: see the file header.  The task re-arms it through
     * bsp_touch_irq_rearm() once the contact handling is done. */
    EXTI->IMR &= ~TOUCH_EXTI_LINE_MSK;

    if (s_touch_sem != NULL)
    {
        xSemaphoreGiveFromISR(s_touch_sem, &higher_prio_task_woken);
        portYIELD_FROM_ISR(higher_prio_task_woken);
    }
}

void bsp_touch_irq_rearm(void)
{
    taskENTER_CRITICAL();
    EXTI->PR  =  TOUCH_EXTI_LINE_MSK;    /* drop edges seen while masked */
    EXTI->IMR |= TOUCH_EXTI_LINE_MSK;
    taskEXIT_CRITICAL();
}

static void touch_exti_mask(void)
{
    EXTI->IMR &= ~TOUCH_EXTI_LINE_MSK;
}

static int touch_exti_init(void)
{
    EXTI_ConfigTypeDef cfg;

    /* SYSCFG must be clocked before HAL_EXTI_SetConfigLine() can write
     * SYSCFG->EXTICR.  Without it the write is silently dropped, EXTI7 keeps
     * its reset mapping (port A) and PH7 edges never reach the NVIC - the
     * touch task then waits on s_touch_sem forever.  Measured before the fix:
     * RCC_APB2ENR bit14 == 0 and SYSCFG->EXTICR[1] == 0x00000000. */
    __HAL_RCC_SYSCFG_CLK_ENABLE();

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

    PRINT_LOG("[TOUCH] EXTI cfg: EXTICR2=0x%08lX IMR=0x%04lX FTSR=0x%04lX prio=%u\r\n",
              (unsigned long)SYSCFG->EXTICR[1], (unsigned long)EXTI->IMR,
              (unsigned long)EXTI->FTSR, (unsigned int)TOUCH_EXTI_PREEMPT_PRIO);

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Coordinate mapping                                                         */
/* -------------------------------------------------------------------------- */
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

static void map_point(uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y)
{
    uint32_t x = raw_x;
    uint32_t y = raw_y;

#if TOUCH_SWAP_XY
    { uint32_t t = x; x = y; y = t; }
#endif

    /* Scale the chip's own touch resolution onto the display canvas.  When the
     * two match (the usual case on this module) this is the identity. */
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

    /* The mapping target is the GRAM window the LCD driver set up.  It is only
     * final once lcd_driver_init() has returned, so wait for the flag instead
     * of silently clamping every coordinate to 0. */
    {
        uint32_t waited = 0U;

        while (lcd_driver_ready() == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(50U));
            waited += 50U;
            if (waited >= 10000U)
            {
                PRINT_LOG("[TOUCH] LCD never came up, mapping to 1x1\r\n");
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
        PRINT_LOG("[TOUCH] semaphore create FAILED\r\n");
        return -1;
    }
    (void)xSemaphoreTake(s_touch_sem, 0U);      /* start empty */

    ret = bsp_gt9147_init();
    if (ret != 0)
    {
        PRINT_LOG("[TOUCH] controller init FAILED (%d)\r\n", ret);
        return -1;
    }

    if (touch_exti_init() != 0)
    {
        PRINT_LOG("[TOUCH] EXTI init FAILED\r\n");
        return -1;
    }

    touch_refresh_geometry();

    PRINT_LOG("[TOUCH] ready: id=%s addr=0x%02X cfg=0x%02X, touch %ux%u -> canvas %ux%u, "
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

int bsp_touch_scan(bsp_touch_point_t *pts, uint8_t max)
{
    gt9147_point_t raw[BSP_TOUCH_MAX_POINTS];
    uint8_t limit = max;
    int n;
    uint8_t i;

    if ((bsp_gt9147_is_ready() == 0) || (pts == NULL) || (max == 0U))
    {
        return -1;
    }

    if (limit > BSP_TOUCH_MAX_POINTS)
    {
        limit = BSP_TOUCH_MAX_POINTS;
    }

    /* The controller read is a bit-banged burst on PH6/PI3; keep line 7 masked
     * across it so the crosstalk it induces cannot fire the touch interrupt. */
    touch_exti_mask();
    n = bsp_gt9147_read(raw, limit);
    if (n < 0)
    {
        s_last_pressed = 0U;
        return -1;
    }

    touch_refresh_geometry();

    for (i = 0U; i < (uint8_t)n; i++)
    {
        map_point(raw[i].x, raw[i].y, &pts[i].x, &pts[i].y);
    }

    if (n > 0)
    {
        s_last_x       = pts[0].x;
        s_last_y       = pts[0].y;
        s_last_pressed = 1U;
    }
    else
    {
        s_last_pressed = 0U;
    }

    return n;
}

void bsp_touch_get(uint16_t *x, uint16_t *y, uint8_t *pressed)
{
    if (x != NULL)       { *x = s_last_x; }
    if (y != NULL)       { *y = s_last_y; }
    if (pressed != NULL) { *pressed = s_last_pressed; }
}

uint32_t bsp_touch_irq_count(void)
{
    return s_irq_count;
}
