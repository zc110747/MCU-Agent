/**
  ******************************************************************************
  * @file    app/serial_cmd.c
  * @brief   USART3 line-oriented command console that mirrors every on-screen
  *          music button, plus a self-test / stress mode and a health report
  *          (CFSR/HFSR) so the player can be verified with NO human in the
  *          loop and stressed automatically.
  *
  *  Why a dedicated task (not polling inside ui_task)
  *  ------------------------------------------------
  *  The console must stay alive and responsive while the audio refill task is
  *  busy decoding -- if it lived inside the LVGL render pump it could be
  *  starved and would not reflect a freeze.  Running it as its own task at the
  *  same priority as ui_task guarantees the verification path is independent of
  *  the display.  Every command calls the SAME player_* API the on-screen
  *  buttons call (music_ui.c), so a serial command IS a button press.
  *
  *  Command set (one command per line; CR or LF terminates)
  *  ------------------------------------------------------
  *    p        toggle play/pause          (player_toggle)
  *    n        next track                 (player_next)
  *    v        previous track             (player_prev)
  *    x        stop                       (player_stop)
  *    +        volume +5                  (player_set_volume)
  *    -        volume -5                  (player_set_volume)
  *    s        seek to 50%               (player_seek_percent)
  *    k<NN>    seek to NN%                (player_seek_percent)
  *    t<NN>    load+play track NN (1-based) (player_load)
  *    z<NN>    stress: NN cycles of toggle/next/prev/pause/stop (type 'q' to abort)
  *    d        diagnostics: print CFSR/HFSR/state (board self-health)
  *    ?        help
  *
  *  The 'd' command lets a host script assert "[DIAG] CFSR=0x00000000" with no
  *  ST-Link / OpenOCD involved -- pure serial verification.
  ******************************************************************************
  */
#include "serial_cmd.h"
#include "bsp_uart.h"
#include "audio_player.h"
#include "log.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define SERIAL_CMD_STACK_WORDS  1024U
#define SERIAL_LINE_MAX         32U

/* Debugger-injected command buffer (see serial_cmd.h).  Plain globals so they
 * are easy to find with arm-none-eabi-nm and to poke with OpenOCD `mwb`. */
volatile char   g_dbg_line[16];
volatile uint8_t g_dbg_pending;

/* ---- tiny decimal parser (avoids newlib atoi/malloc) -------------------- */
static uint32_t parse_uint(const char *s)
{
    uint32_t v = 0U;
    while ((*s >= '0') && (*s <= '9'))
    {
        v = v * 10U + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

/* ---- forward declarations ------------------------------------------------ */
static void serial_dispatch(const char *line);
static void serial_run_stress(uint32_t cycles);

/* ---- command dispatch ---------------------------------------------------- */
static void serial_dispatch(const char *line)
{
    char cmd = line[0];

    if (cmd == '?' || cmd == 'h' || cmd == 'H')
    {
        PRINT_LOG("[SER ] cmds: p=toggle n=next v=prev x=stop +=vol- -=vol- "
                  "s=seek50 kNN=seek%% tNN=track zNN=stress d=diag\r\n");
        return;
    }

    /* Board self-health: CFSR/HFSR are readable from any mode.  A non-zero
     * CFSR means a fault happened (the board would otherwise be frozen). */
    if (cmd == 'd' || cmd == 'D')
    {
        volatile uint32_t cfsr = *((volatile uint32_t *)0xE000ED28);
        volatile uint32_t hfsr = *((volatile uint32_t *)0xE000ED2C);
        PRINT_LOG("[DIAG] CFSR=0x%08X HFSR=0x%08X state=%d tracks=%lu vol=%u\r\n",
                  (unsigned)cfsr, (unsigned)hfsr, (int)player_state(),
                  (unsigned long)player_count(), (unsigned)player_get_volume());
        return;
    }

    if (cmd == 'p' || cmd == 'P')
    {
        player_toggle();
        PRINT_LOG("[CMD] toggle -> state=%d\r\n", (int)player_state());
        return;
    }
    if (cmd == 'n' || cmd == 'N')
    {
        player_next();
        PRINT_LOG("[CMD] next   -> %d/%lu\r\n",
                  player_current_index() + 1, (unsigned long)player_count());
        return;
    }
    if (cmd == 'v' || cmd == 'V')
    {
        player_prev();
        PRINT_LOG("[CMD] prev   -> %d/%lu\r\n",
                  player_current_index() + 1, (unsigned long)player_count());
        return;
    }
    if (cmd == 'x' || cmd == 'X')
    {
        player_stop();
        PRINT_LOG("[CMD] stop\r\n");
        return;
    }
    if (cmd == '+')
    {
        player_set_volume((uint8_t)(player_get_volume() + 5U));
        PRINT_LOG("[CMD] vol+ -> %u\r\n", (unsigned)player_get_volume());
        return;
    }
    if (cmd == '-')
    {
        uint8_t nv = (player_get_volume() > 5U) ? (player_get_volume() - 5U) : 0U;
        player_set_volume(nv);
        PRINT_LOG("[CMD] vol- -> %u\r\n", (unsigned)player_get_volume());
        return;
    }
    if (cmd == 's' || cmd == 'S')
    {
        player_seek_percent(50U);
        PRINT_LOG("[CMD] seek 50%%\r\n");
        return;
    }
    if (cmd == 'k' || cmd == 'K')
    {
        uint32_t pct = parse_uint(line + 1);
        player_seek_percent(pct);
        PRINT_LOG("[CMD] seek %lu%%\r\n", (unsigned long)pct);
        return;
    }
    if (cmd == 't' || cmd == 'T')
    {
        uint32_t trk = parse_uint(line + 1);
        if (trk == 0U) { trk = 1U; }
        player_load(trk - 1U, 1U);
        PRINT_LOG("[CMD] track %lu -> %d/%lu\r\n", (unsigned long)trk,
                  player_current_index() + 1, (unsigned long)player_count());
        return;
    }
    if (cmd == 'z' || cmd == 'Z')
    {
        uint32_t cycles = parse_uint(line + 1);
        if (cycles == 0U) { cycles = 10U; }
        serial_run_stress(cycles);
        return;
    }

    PRINT_LOG("[SER ] unknown '%s' (send '?')\r\n", line);
}

/* ---- stress / self-test loop -------------------------------------------- */
static void serial_run_stress(uint32_t cycles)
{
    PRINT_LOG("[STR] stress start cycles=%lu\r\n", (unsigned long)cycles);
    for (uint32_t i = 1U; i <= cycles; i++)
    {
        /* allow an early quit without needing a reset */
        uint8_t c;
        if (uart_getchar_nowait(&c) && ((c == 'q') || (c == 'Q')))
        {
            PRINT_LOG("[STR] aborted at cycle %lu\r\n", (unsigned long)i);
            break;
        }
        player_toggle(); vTaskDelay(pdMS_TO_TICKS(80));   /* play  */
        player_next();   vTaskDelay(pdMS_TO_TICKS(80));
        player_prev();   vTaskDelay(pdMS_TO_TICKS(80));
        player_toggle(); vTaskDelay(pdMS_TO_TICKS(80));   /* pause */
        player_stop();   vTaskDelay(pdMS_TO_TICKS(80));
        PRINT_LOG("[STR] cycle=%lu/%lu ok\r\n", (unsigned long)i,
                  (unsigned long)cycles);
    }
    PRINT_LOG("[STR] stress done\r\n");
}

/* ---- task body ----------------------------------------------------------- */
void serial_cmd_task(void *arg)
{
    (void)arg;
    static uint8_t line[SERIAL_LINE_MAX];
    uint8_t idx = 0U;

    PRINT_LOG("[SER ] console ready (send '?' for help)\r\n");
    for (;;)
    {
        /* Debugger-injected command (TX-only serial work-around).  A host with
         * no working RX line can still drive the console by writing a NUL-
         * terminated string into g_dbg_line and then setting g_dbg_pending=1
         * (e.g. via OpenOCD `mwb`).  The command runs through the SAME dispatch
         * path a UART byte would take, so every key + the stress loop work
         * without a physical RX line.  Inert unless a debugger writes it. */
        if (g_dbg_pending != 0U)
        {
            g_dbg_pending = 0U;
            serial_dispatch((const char *)g_dbg_line);
        }

        uint8_t c;
        if (uart_getchar_nowait(&c) != 0)
        {
            if ((c == '\r') || (c == '\n'))
            {
                if (idx > 0U)
                {
                    line[idx] = '\0';
                    serial_dispatch((const char *)line);
                    idx = 0U;
                }
            }
            else if ((c == '\b') || (c == 0x7FU))   /* backspace */
            {
                if (idx > 0U) { idx--; }
            }
            else if (idx < (SERIAL_LINE_MAX - 1U))
            {
                line[idx++] = c;
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

void serial_cmd_init(void)
{
    if (xTaskCreate(serial_cmd_task, "serial", SERIAL_CMD_STACK_WORDS, NULL,
                    tskIDLE_PRIORITY + 2, NULL) != pdPASS)
    {
        PRINT_LOG("[SER ] task create failed\r\n");
    }
}
