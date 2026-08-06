/* ---------------------------------------------------------------------------
 * STM32H743ZIT6 + TinyUSB - USB CDC (virtual COM port) send/receive test
 *
 * Behaviour once the board enumerates as a serial port:
 *   - every byte you type is echoed straight back, so a terminal behaves
 *     the way you would expect;
 *   - a complete line (terminated by CR or LF) is treated as a command -
 *     type "help" for the list;
 *   - an optional 1 Hz heartbeat line proves the device -> host direction
 *     keeps working even when you are not typing.
 *
 * LED (PG7) doubles as a link indicator:
 *   fast blink  (4 Hz)   - not enumerated
 *   slow blink  (1 Hz)   - enumerated and running
 *   very slow   (0.4 Hz) - bus suspended
 * -------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "bsp.h"
#include "tusb.h"
#include "sdcard.h"   /* SDMMC1 / SD card block interface */
#include "sd_app.h"   /* SD-card FatFs app: init + CDC file commands */

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */
typedef enum {
  LINK_UNMOUNTED = 0,
  LINK_MOUNTED,
  LINK_SUSPENDED,
} link_state_t;

static volatile link_state_t s_link = LINK_UNMOUNTED;

static uint32_t s_rx_bytes = 0;
static uint32_t s_tx_bytes = 0;
static uint32_t s_lines    = 0;
static bool     s_heartbeat = true;

#define LINE_MAX 128
static char     s_line[LINE_MAX];
static uint16_t s_line_len = 0;

/* ------------------------------------------------------------------------ */
/* Output helpers                                                            */
/* ------------------------------------------------------------------------ */

/* Write everything, pumping the stack while the TX FIFO drains. Bounded so a
 * host that stops reading can never wedge the main loop. */
static void cdc_write_all(const void* buf, uint32_t len) {
  const uint8_t* p = (const uint8_t*) buf;
  uint32_t sent = 0;
  const uint32_t deadline = board_millis() + 100;

  while (sent < len) {
    if (!tud_cdc_connected()) return;

    uint32_t n = tud_cdc_write(p + sent, len - sent);
    sent += n;
    tud_cdc_write_flush();

    if (sent < len) {
      tud_task();
      if ((int32_t)(board_millis() - deadline) >= 0) break;  /* give up */
    }
  }
  s_tx_bytes += sent;
}

static void cdc_puts(const char* s) {
  cdc_write_all(s, (uint32_t) strlen(s));
}

static void cdc_printf(const char* fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) {
    cdc_write_all(buf, (uint32_t) ((n < (int) sizeof(buf)) ? n : (int) sizeof(buf) - 1));
  }
}

/* Route printf()/puts() to the USB serial port as well (see syscalls.c). */
int _write(int fd, char* ptr, int len) {
  (void) fd;
  if (len > 0) cdc_write_all(ptr, (uint32_t) len);
  return len;
}

/* ------------------------------------------------------------------------ */
/* Commands                                                                  */
/* ------------------------------------------------------------------------ */
static void print_banner(void) {
  cdc_puts("\r\n");
  cdc_puts("=============================================\r\n");
  cdc_puts(" STM32H743ZIT6 + TinyUSB  USB CDC test\r\n");
  cdc_printf(" SYSCLK %lu MHz   HCLK %lu MHz   USB %s\r\n",
             (unsigned long) (g_sysclk_hz / 1000000U),
             (unsigned long) (g_hclk_hz / 1000000U),
             (BOARD_TUD_RHPORT == 0) ? "OTG_FS PA11/PA12" : "OTG_HS PB14/PB15");
  cdc_puts(" Type 'help' for commands. Everything you\r\n"
           " type is echoed back as it arrives.\r\n");
  cdc_puts("=============================================\r\n> ");
}

static void cmd_help(void) {
  cdc_puts("\r\n"
           "commands:\r\n"
           "  help          this text\r\n"
           "  stats         byte counters and uptime\r\n"
           "  clk           clock tree summary\r\n"
           "  hb            toggle the 1 Hz heartbeat line\r\n"
           "  led on|off    drive the user LED (PG7)\r\n"
           "  flood <n>     send n lines back to back (throughput test)\r\n"
           "  sd            SD card / filesystem status\r\n"
           "  ls            list files on the SD card\r\n"
           "  cat <file>    print a file from the SD card\r\n"
           "  remount       re-read the SD card filesystem (after host edits)\r\n");
}

static void cmd_stats(void) {
  uint32_t up = board_millis() / 1000U;
  cdc_printf("\r\nuptime %lus  rx %lu bytes  tx %lu bytes  lines %lu  heartbeat %s\r\n",
             (unsigned long) up,
             (unsigned long) s_rx_bytes,
             (unsigned long) s_tx_bytes,
             (unsigned long) s_lines,
             s_heartbeat ? "on" : "off");
}

static void cmd_clk(void) {
  cdc_printf("\r\nSYSCLK   %lu Hz\r\n"
             "HCLK     %lu Hz\r\n"
             "PLL src  HSE 25 MHz crystal (passive)\r\n"
             "USB clk  HSI48 + CRS auto-trim from host SOF\r\n",
             (unsigned long) g_sysclk_hz,
             (unsigned long) g_hclk_hz);
}

static void cmd_flood(const char* arg) {
  uint32_t n = (uint32_t) strtoul(arg, NULL, 10);
  if (n == 0)     n = 10;
  if (n > 10000)  n = 10000;

  uint32_t t0 = board_millis();
  for (uint32_t i = 1; i <= n; i++) {
    cdc_printf("line %lu/%lu  the quick brown fox jumps over the lazy dog\r\n",
               (unsigned long) i, (unsigned long) n);
    tud_task();
  }
  uint32_t dt = board_millis() - t0;
  cdc_printf("sent %lu lines in %lu ms\r\n", (unsigned long) n, (unsigned long) dt);
}

static void handle_line(char* line) {
  s_lines++;

  /* trim leading spaces */
  while (*line == ' ') line++;

  if (*line == '\0') {
    /* bare Enter - just reprint the prompt */
  } else if (strcmp(line, "help") == 0) {
    cmd_help();
  } else if (strcmp(line, "stats") == 0) {
    cmd_stats();
  } else if (strcmp(line, "clk") == 0) {
    cmd_clk();
  } else if (strcmp(line, "hb") == 0) {
    s_heartbeat = !s_heartbeat;
    cdc_printf("\r\nheartbeat %s\r\n", s_heartbeat ? "on" : "off");
  } else if (strcmp(line, "led on") == 0) {
    board_led_write(true);
    cdc_puts("\r\nled on\r\n");
  } else if (strcmp(line, "led off") == 0) {
    board_led_write(false);
    cdc_puts("\r\nled off\r\n");
  } else if (strncmp(line, "flood", 5) == 0) {
    cmd_flood(line + 5);
  } else if (strcmp(line, "sd") == 0) {
    cmd_sd();
  } else if (strcmp(line, "ls") == 0) {
    cmd_ls();
  } else if (strncmp(line, "cat ", 4) == 0) {
    cmd_cat(line + 4);
  } else if (strcmp(line, "remount") == 0) {
    cmd_remount();
  } else {
    cdc_printf("\r\nunknown command: '%s'  (try 'help')\r\n", line);
  }

  cdc_puts("> ");
}

/* ------------------------------------------------------------------------ */
/* Tasks                                                                     */
/* ------------------------------------------------------------------------ */
static void cdc_task(void) {
  if (!tud_cdc_available()) return;

  uint8_t buf[64];
  uint32_t count = tud_cdc_read(buf, sizeof(buf));
  s_rx_bytes += count;

  for (uint32_t i = 0; i < count; i++) {
    char c = (char) buf[i];

    if (c == '\r' || c == '\n') {
      cdc_puts("\r\n");
      s_line[s_line_len] = '\0';
      s_line_len = 0;
      handle_line(s_line);
    } else if (c == '\b' || c == 0x7F) {          /* backspace / delete */
      if (s_line_len > 0) {
        s_line_len--;
        cdc_puts("\b \b");
      }
    } else {
      if (s_line_len < LINE_MAX - 1) {
        s_line[s_line_len++] = c;
      }
      cdc_write_all(&c, 1);                       /* echo as it arrives */
    }
  }
}

static void heartbeat_task(void) {
  static uint32_t next = 0;
  if (!s_heartbeat || s_link != LINK_MOUNTED || !tud_cdc_connected()) return;
  if ((int32_t)(board_millis() - next) < 0) return;
  next = board_millis() + 1000;

  cdc_printf("\r\n[hb] up=%lus rx=%lu tx=%lu\r\n> ",
             (unsigned long) (board_millis() / 1000U),
             (unsigned long) s_rx_bytes,
             (unsigned long) s_tx_bytes);
}

static void led_task(void) {
  static uint32_t next = 0;
  uint32_t period;

  switch (s_link) {
    case LINK_MOUNTED:   period = 500;  break;   /* 1 Hz    */
    case LINK_SUSPENDED: period = 1250; break;   /* 0.4 Hz  */
    default:             period = 125;  break;   /* 4 Hz    */
  }

  if ((int32_t)(board_millis() - next) < 0) return;
  next = board_millis() + period;
  board_led_toggle();
}

/* ------------------------------------------------------------------------ */
int main(void) {
  bsp_init();
  sdcard_init();          /* bring up SDMMC1 and the SD card */
  fatfs_init();           /* mount (or format+seed) the FAT volume */

  /* VBUS sensing off: the device assumes bus power is always present, which
   * is what you want when VBUS is not routed to the MCU. Must be set before
   * tud_init(). */
  tud_configure_dwc2_t dwc2_cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
  dwc2_cfg.vbus_sensing = false;
  tud_configure(BOARD_TUD_RHPORT, TUD_CFGID_DWC2, &dwc2_cfg);

  const tusb_rhport_init_t rh_init = {
      .role  = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_FULL,
  };
  tusb_init(BOARD_TUD_RHPORT, &rh_init);

  while (1) {
    tud_task();        /* TinyUSB device stack - must be called often */
    cdc_task();
    heartbeat_task();
    led_task();
  }
}

/* ------------------------------------------------------------------------ */
/* TinyUSB device callbacks                                                  */
/* ------------------------------------------------------------------------ */
void tud_mount_cb(void)   { s_link = LINK_MOUNTED; }
void tud_umount_cb(void)  { s_link = LINK_UNMOUNTED; }

void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  s_link = LINK_SUSPENDED;
}

void tud_resume_cb(void)  { s_link = LINK_MOUNTED; }

/* Fired when the host opens or closes the port (DTR assert/deassert). */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
  (void) itf;
  (void) rts;
  if (dtr) {
    /* Small settle delay: some terminals drop the first bytes right after
     * they assert DTR. */
    board_delay_ms(10);
    s_line_len = 0;
    print_banner();
  }
}
