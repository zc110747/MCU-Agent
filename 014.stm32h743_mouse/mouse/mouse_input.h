/* ---------------------------------------------------------------------------
 * mouse_input.h - per-source button state.
 *
 * The two button sources are kept strictly separate and combined by OR:
 *
 *     combined = physical | uart
 *
 * This is the whole point of the module. A single shared "buttons" variable
 * would let whichever source releases last clear a button the other source is
 * still holding - press LEFT on the GPIO button, press LEFT over UART, release
 * the GPIO one, and the cursor stops dragging even though the host explicitly
 * asked for a held button. OR-ing independent shadows makes that impossible.
 *
 * "Long press" has no HID meaning: a mouse button that is held simply stays
 * down, there is no second conceptual state. bsp_button.c therefore exposes
 * the hold duration for diagnostics only - see docs/protocol.md.
 * -------------------------------------------------------------------------*/

#ifndef MOUSE_INPUT_H_
#define MOUSE_INPUT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t physical;   /* shadow of the GPIO buttons   (MOUSE_BUTTON_*) */
    uint8_t uart;       /* shadow of the UART commands  (MOUSE_BUTTON_*) */
    uint8_t combined;   /* physical | uart, kept in sync on every write  */
} mouse_input_t;

/* Reset both shadows to "nothing pressed". */
void mouse_input_init(void);

/* Overwrite one source's shadow. Only the bits in MOUSE_BUTTON_MASK survive. */
void mouse_input_set_physical(uint8_t buttons);
void mouse_input_set_uart(uint8_t buttons);

/* Clear the UART shadow only - the physical buttons are untouched, which is
 * what makes "RELEASE,ALL" safe while a finger is still on a key. */
void mouse_input_clear_uart(void);

/* Read-only snapshot. The pointer is valid for the lifetime of the program;
 * `combined` is recomputed on every setter, so it is never stale. */
const mouse_input_t *mouse_input_get(void);

#ifdef __cplusplus
}
#endif

#endif /* MOUSE_INPUT_H_ */
