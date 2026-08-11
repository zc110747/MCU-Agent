/**
  ******************************************************************************
  * @file    bsp_key.h
  * @brief   Virtual key abstraction layer.
  *
  *  The LXB743ZI-P1 board carries no user push-buttons, so every key event in
  *  this project is *injected* - today from the serial console (USART1 or the
  *  USB CDC port), tomorrow from real GPIO if a keypad is ever wired up.
  *
  *  Two views of the same input are exported:
  *
  *    - an event queue  (bsp_key_pop)   : edge driven, drives the LVGL menus
  *    - a level bitmask (bsp_key_state) : held state, drives the NES pad
  *
  *  bsp_key_tap() presses a key and schedules its release, which is what a
  *  line-oriented console command needs: "key a" must look like a real, short
  *  button press to the emulator, not a key that stays down forever.
  ******************************************************************************
  */
#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
  * @brief  Logical keys.
  *
  * The first eight map 1:1 onto the NES controller; the last three are UI-only
  * and are what the menu pages listen to.  OK/BACK are aliased onto A/B by the
  * console parser so a game pad can drive the menu too.
  */
typedef enum
{
    KEY_UP = 0,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_A,
    KEY_B,
    KEY_SELECT,
    KEY_START,
    KEY_OK,
    KEY_BACK,
    KEY_MENU,
    KEY_COUNT
} key_id_t;

typedef enum
{
    KEY_EV_DOWN = 0,
    KEY_EV_UP
} key_edge_t;

typedef struct
{
    uint8_t id;     /**< key_id_t   */
    uint8_t edge;   /**< key_edge_t */
} key_event_t;

/** Default press length used by bsp_key_tap() when 0 is passed. */
#define KEY_TAP_DEFAULT_MS      60U

void        bsp_key_init(void);

/** Inject a press/release edge (idempotent: repeated downs are swallowed). */
void        bsp_key_inject(key_id_t id, key_edge_t edge);

/** Press now, release automatically after hold_ms (0 -> KEY_TAP_DEFAULT_MS). */
void        bsp_key_tap(key_id_t id, uint16_t hold_ms);

/** Release every key and flush the event queue. */
void        bsp_key_release_all(void);

/** Service the auto-release timers.  Call from the main loop. */
void        bsp_key_poll(void);

/** Pop one edge event; returns 1 when *ev was filled, 0 when the queue is dry. */
int         bsp_key_pop(key_event_t *ev);

/** Bitmask of currently held keys: bit N = (1u << key_id_t). */
uint32_t    bsp_key_state(void);

const char *bsp_key_name(key_id_t id);

/** Parse "up" / "a" / "start" ... ; returns -1 when the name is unknown. */
int         bsp_key_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_KEY_H */
