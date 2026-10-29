/**
 ******************************************************************************
 * @file    bsp_key.c
 * @brief   Polled, debounced keypad for the three player keys.
 *
 *  Debounce state is kept per key as (last stable level, mismatch counter).
 *  A key only changes its stable level after BSP_KEY_DEBOUNCE_SAMPLES
 *  consecutive samples disagree with it, and an event is emitted on the
 *  press edge only -- holding a key down does not repeat.
 ******************************************************************************
 */
#include "bsp_key.h"
#include "stm32f4xx_hal.h"

#define KEY_COUNT 4U
#define BSP_KEY_DEBOUNCE_SAMPLES 2U

enum {
    KEY_IDX_UP = 0, /* PA0  , active high */
    KEY_IDX_K0,     /* PH3  , active low  */
    KEY_IDX_K1,     /* PH2  , active low  (unused) */
    KEY_IDX_K2,     /* PC13 , active low  */
};

typedef struct
{
    uint8_t stable;   /* debounced level: 1 = pressed */
    uint8_t mismatch; /* consecutive samples that disagree */
} KEY_STATE_T;

static KEY_STATE_T s_keys[KEY_COUNT] = {0};

static uint8_t key_read_raw(uint32_t index)
{
    switch (index)
    {
    case KEY_IDX_UP:
        return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) ? 1U : 0U;
    case KEY_IDX_K0:
        return (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_RESET) ? 1U : 0U;
    case KEY_IDX_K1:
        return (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_2) == GPIO_PIN_RESET) ? 1U : 0U;
    case KEY_IDX_K2:
        return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET) ? 1U : 0U;
    default:
        return 0U;
    }
}

void bsp_key_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* KEY0 / KEY1 on PH3 / PH2, KEY2 on PC13: pulled up, pressed = low. */
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOH, &gpio);

    gpio.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* KEY_UP (PA0): pulled down, pressed = high. */
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Pin  = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* Seed the state with the current levels so the first scan cannot report a
     * phantom press. */
    for (uint32_t i = 0U; i < KEY_COUNT; i++)
    {
        s_keys[i].stable   = key_read_raw(i);
        s_keys[i].mismatch = 0U;
    }
}

BSP_KEY_EVENT_T bsp_key_scan(void)
{
    BSP_KEY_EVENT_T event = BSP_KEY_EVENT_NONE;

    for (uint32_t i = 0U; i < KEY_COUNT; i++)
    {
        uint8_t raw = key_read_raw(i);

        if (raw == s_keys[i].stable)
        {
            s_keys[i].mismatch = 0U;
            continue;
        }

        s_keys[i].mismatch++;
        if (s_keys[i].mismatch < BSP_KEY_DEBOUNCE_SAMPLES)
        {
            continue;
        }

        s_keys[i].mismatch = 0U;
        s_keys[i].stable   = raw;

        if (raw == 0U)
        {
            continue; /* release edge: nothing to report */
        }

        if ((i == KEY_IDX_UP) && (event == BSP_KEY_EVENT_NONE))
        {
            event = BSP_KEY_EVENT_PLAY_PAUSE;
        }
        else if ((i == KEY_IDX_K0) && (event == BSP_KEY_EVENT_NONE))
        {
            event = BSP_KEY_EVENT_NEXT;
        }
        else if ((i == KEY_IDX_K2) && (event == BSP_KEY_EVENT_NONE))
        {
            event = BSP_KEY_EVENT_PREV;
        }
    }

    return event;
}
