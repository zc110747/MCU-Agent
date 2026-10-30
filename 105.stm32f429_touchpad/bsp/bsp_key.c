/**
 ******************************************************************************
 * @file    bsp_key.c
 * @brief   Polled, debounced keypad for the four player keys.
 *
 *  Debounce state is kept per key as (last stable level, mismatch counter).
 *  A key only changes its stable level after BSP_KEY_DEBOUNCE_SAMPLES
 *  consecutive samples disagree with it.  Hold and press-edge views are
 *  derived from that stable level, so neither can report a bouncing edge.
 ******************************************************************************
 */
#include "bsp_key.h"
#include "stm32f4xx_hal.h"

typedef struct
{
    uint8_t stable;     /* debounced level: 1 = pressed */
    uint8_t mismatch;   /* consecutive samples that disagree */
    uint8_t edge;       /* latched press edge, cleared by bsp_key_edge() */
} key_state_t;

static key_state_t s_keys[BSP_KEY_COUNT];

static uint8_t key_read_raw(bsp_key_id_t id)
{
    switch (id)
    {
    case BSP_KEY_UP:    /* PA0, active high */
        return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) ? 1U : 0U;
    case BSP_KEY_0:     /* PH3, active low */
        return (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_RESET) ? 1U : 0U;
    case BSP_KEY_1:     /* PH2, active low */
        return (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_2) == GPIO_PIN_RESET) ? 1U : 0U;
    case BSP_KEY_2:     /* PC13, active low */
        return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET) ? 1U : 0U;
    default:
        return 0U;
    }
}

void bsp_key_init(void)
{
    GPIO_InitTypeDef gpio;

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

    /* Seed the state with the current levels so the first poll cannot report a
     * phantom press. */
    for (uint32_t i = 0U; i < (uint32_t)BSP_KEY_COUNT; i++)
    {
        s_keys[i].stable   = key_read_raw((bsp_key_id_t)i);
        s_keys[i].mismatch = 0U;
        s_keys[i].edge     = 0U;
    }
}

void bsp_key_poll(void)
{
    for (uint32_t i = 0U; i < (uint32_t)BSP_KEY_COUNT; i++)
    {
        uint8_t raw = key_read_raw((bsp_key_id_t)i);

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

        if (raw != 0U)
        {
            s_keys[i].edge = 1U;   /* press edge: released by bsp_key_edge() */
        }
    }
}

uint8_t bsp_key_level(bsp_key_id_t id)
{
    if ((uint32_t)id >= (uint32_t)BSP_KEY_COUNT)
    {
        return 0U;
    }
    return s_keys[id].stable;
}

uint8_t bsp_key_edge(bsp_key_id_t id)
{
    if ((uint32_t)id >= (uint32_t)BSP_KEY_COUNT)
    {
        return 0U;
    }
    if (s_keys[id].edge == 0U)
    {
        return 0U;
    }
    s_keys[id].edge = 0U;
    return 1U;
}
