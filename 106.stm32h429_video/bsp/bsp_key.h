/**
 ******************************************************************************
 * @file    bsp_key.h
 * @brief   On-board keys, polled from the input task (never blocking).
 *
 *          Wiring (ALIENTEK Apollo F429 -- same four keys as
 *          104.stm32f429_music):
 *
 *            KEY_UP  PA0   active high
 *            KEY0    PH3   active low
 *            KEY1    PH2   active low
 *            KEY2    PC13  active low
 *
 *          Debounce: the caller must invoke bsp_key_poll() at a fixed period
 *          (BSP_KEY_SCAN_PERIOD_MS).  A key only changes its debounced level
 *          after BSP_KEY_DEBOUNCE_SAMPLES consecutive samples agree.
 *
 *          Two views of the same state are offered:
 *            - bsp_key_level()  current debounced level  (hold = drag)
 *            - bsp_key_edge()   consumed press edge       (click actions)
 ******************************************************************************
 */
#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_KEY_SCAN_PERIOD_MS   10U
#define BSP_KEY_DEBOUNCE_SAMPLES 2U

typedef enum
{
    BSP_KEY_UP = 0,     /* PA0  */
    BSP_KEY_0,          /* PH3  */
    BSP_KEY_1,          /* PH2  */
    BSP_KEY_2,          /* PC13 */
    BSP_KEY_COUNT
} bsp_key_id_t;

/** Configure the four key pins (no interrupt is used). */
void bsp_key_init(void);

/** Sample all keys once; call every BSP_KEY_SCAN_PERIOD_MS. */
void bsp_key_poll(void);

/**
 * @brief  Debounced level of one key.
 * @retval 1 while the key is held down, 0 when released.
 */
uint8_t bsp_key_level(bsp_key_id_t id);

/**
 * @brief  Consume the press edge of one key.
 * @retval 1 the first call after a debounced press, 0 afterwards.
 */
uint8_t bsp_key_edge(bsp_key_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_KEY_H */
