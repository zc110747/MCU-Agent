/* ---------------------------------------------------------------------------
 * mouse_input.c - see mouse_input.h for the merge rule and why it matters.
 * -------------------------------------------------------------------------*/

#include "mouse_input.h"
#include "mouse_report.h"

static mouse_input_t g_input;

static void refresh_combined(void)
{
    g_input.combined = (uint8_t) (g_input.physical | g_input.uart);
}

void mouse_input_init(void)
{
    g_input.physical = 0u;
    g_input.uart     = 0u;
    g_input.combined = 0u;
}

void mouse_input_set_physical(uint8_t buttons)
{
    g_input.physical = (uint8_t) (buttons & MOUSE_BUTTON_MASK);
    refresh_combined();
}

void mouse_input_set_uart(uint8_t buttons)
{
    g_input.uart = (uint8_t) (buttons & MOUSE_BUTTON_MASK);
    refresh_combined();
}

void mouse_input_clear_uart(void)
{
    g_input.uart = 0u;
    refresh_combined();
}

const mouse_input_t *mouse_input_get(void)
{
    return &g_input;
}
