/**
 * @file rt_key_scan.c
 * @brief Serial "virtual gamepad" (see rt_key_scan.h).
 *
 * The character stream that feeds the msh shell is tapped here.  The shell owns
 * bsp_uart_getchar() and must keep receiving every byte, so this module does
 * not steal input: a `nes keys on` command switches the emulator into
 * gamepad-capture mode, and the msh command handler forwards each character to
 * key_scan_feed() before discarding it.  Outside capture mode the decoder is
 * idle and the shell behaves normally.
 *
 * The mask is a single 32-bit word written with one store and read with one
 * load, so no lock is needed between the console thread and the emulator
 * thread.
 */
#include "rt_key_scan.h"

#include <rtthread.h>

/* Single-writer (console/msh thread) / single-reader (emulator thread) word.
   volatile is sufficient here: pressing a button is not ordering-sensitive
   against any other memory this module touches, and the load/store are
   naturally atomic on Cortex-M85. */
static volatile uint32_t g_mask      = 0U;
static volatile uint32_t g_events    = 0U;

/* Exposed so `nes keys` can report whether capture is armed. */
static volatile rt_bool_t g_capture  = RT_FALSE;

static const char * const g_names[8] =
{
    "UP", "DOWN", "LEFT", "RIGHT", "SELECT", "START", "A", "B"
};

static const uint32_t g_bits[8] =
{
    TP_UP, TP_DOWN, TP_LEFT, TP_RIGHT, TP_SELECT, TP_START, TP_A, TP_B
};

void key_scan_init (void)
{
    g_mask    = 0U;
    g_events  = 0U;
    g_capture = RT_FALSE;
}

uint32_t key_scan (void)
{
    return g_mask;
}

void key_scan_set (uint32_t mask)
{
    g_mask = mask & (uint32_t) (TP_UP | TP_DOWN | TP_LEFT | TP_RIGHT |
                                TP_SELECT | TP_START | TP_A | TP_B);
    g_events++;
}

uint32_t key_scan_count (void)
{
    return g_events;
}

const char * key_scan_name (uint32_t mask)
{
    /* Round-robin through one static buffer per invocation slot so two calls in
       the same rt_kprintf() argument list do not clobber each other. */
    static char buf[2][32];
    static uint32_t slot = 0U;

    char *      out = buf[slot];
    rt_size_t   len = 0U;
    uint32_t    i;

    slot = (slot + 1U) & 1U;
    out[0] = '\0';

    for (i = 0U; i < 8U; i++)
    {
        if (0U == (mask & g_bits[i]))
        {
            continue;
        }

        if (0U != len)
        {
            out[len++] = '|';
        }

        for (const char * p = g_names[i]; ('\0' != *p) && (len < 30U); p++)
        {
            out[len++] = *p;
        }
    }

    out[len] = '\0';
    return out;
}

rt_bool_t key_scan_capture_enabled (void)
{
    return g_capture;
}

void key_scan_capture (rt_bool_t on)
{
    g_capture = on;

    /* Leaving capture mode must not leave a button stuck down. */
    if (!on)
    {
        g_mask = 0U;
    }
}

/**
 * @brief Decode one console character into a gamepad bit change.
 *
 * Returns RT_TRUE when the character was consumed as a gamepad key (the shell
 * should then drop it), RT_FALSE when it is ordinary shell input.
 *
 * Held-button semantics: a key press sets its bit and it stays set until the
 * same key is pressed again or space clears everything.  Games that need
 * momentary presses work with tap-tap; games needing holds (running in Mario)
 * work because the bit persists.  The space bar is the universal release.
 */
rt_bool_t key_scan_feed (char c)
{
    uint32_t bit = 0U;

    switch (c)
    {
        case 'w': case 'W': bit = TP_UP;     break;
        case 's': case 'S': bit = TP_DOWN;   break;
        case 'a': case 'A': bit = TP_LEFT;   break;
        case 'd': case 'D': bit = TP_RIGHT;  break;
        case 'k': case 'K': bit = TP_A;      break;
        case 'j': case 'J': bit = TP_B;      break;
        case 'n': case 'N': bit = TP_SELECT; break;
        case 'm': case 'M': bit = TP_START;  break;

        case ' ':
        case 'x':
        case 'X':
        {
            /* Release everything. */
            g_mask = 0U;
            g_events++;
            return RT_TRUE;
        }

        default:
            return RT_FALSE;
    }

    g_mask ^= bit;   /* toggle: press again to release that one button */
    g_events++;
    return RT_TRUE;
}

uint32_t key_scan_to_nes_joypad (uint32_t mask)
{
    /* NES shift-register order, MSB first, active high = NOT pressed:
         bit15 A1, bit14 B1, bit13 SE1(select), bit12 ST1(start),
         bit11 U1, bit10 D1, bit9  L1, bit8  R1
       nes_read_joypad() returns 1 for "not pressed" (it tests the bit and
       inverts with ?:1:0 at the call site), so a released button must be 1. */
    uint32_t pad = 0U;

    if (0U == (mask & TP_A))      { pad |= (1U << 15); }
    if (0U == (mask & TP_B))      { pad |= (1U << 14); }
    if (0U == (mask & TP_SELECT)) { pad |= (1U << 13); }
    if (0U == (mask & TP_START))  { pad |= (1U << 12); }
    if (0U == (mask & TP_UP))     { pad |= (1U << 11); }
    if (0U == (mask & TP_DOWN))   { pad |= (1U << 10); }
    if (0U == (mask & TP_LEFT))   { pad |= (1U << 9);  }
    if (0U == (mask & TP_RIGHT))  { pad |= (1U << 8);  }

    /* Player 2 shares the same state (the board has one pad). */
    pad |= (pad >> 8);

    return pad & 0xFFFFU;
}
