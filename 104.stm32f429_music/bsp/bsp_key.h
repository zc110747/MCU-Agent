/**
  ******************************************************************************
  * @file    bsp_key.h
  * @brief   On-board keys, polled from a task (never blocking).
  *
  *          Wiring (ALIENTEK Apollo F429):
  *            KEY0   PH3   active low   -> next track
  *            KEY2   PC13  active low   -> previous track
  *            KEY_UP PA0   active high  -> play / pause
  *          KEY1 (PH2) is wired but not used by the player; it is still sampled
  *          so the debounce table stays uniform.
  *
  *          Debounce: the caller must invoke bsp_key_scan() at a fixed period
  *          (BSP_KEY_SCAN_PERIOD_MS).  A key is accepted only after two
  *          consecutive samples agree, i.e. after ~2 x the scan period.
  ******************************************************************************
  */
#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_KEY_SCAN_PERIOD_MS   10U

typedef enum
{
  BSP_KEY_EVENT_NONE = 0,
  BSP_KEY_EVENT_PLAY_PAUSE,   /* KEY_UP  / PA0 */
  BSP_KEY_EVENT_NEXT,         /* KEY0    / PH3 */
  BSP_KEY_EVENT_PREV,         /* KEY2    / PC13 */
} BSP_KEY_EVENT_T;

/** Configure the four key pins.  No interrupt is used. */
void bsp_key_init(void);

/**
  * @brief  Sample all keys once and return the press edge, if any.
  * @retval BSP_KEY_EVENT_NONE when nothing was pressed since the last call.
  */
BSP_KEY_EVENT_T bsp_key_scan(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_KEY_H */
