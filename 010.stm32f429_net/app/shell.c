/**
  ******************************************************************************
  * @file    shell.c
  * @brief   UART command-line shell with interrupt RX, line editing, history,
  *          and a single parsing thread. Parser is decoupled from the transport
  *          (shell_exec) so telnet can reuse it later.
  ******************************************************************************
  */
#include "shell.h"
#include "bsp_uart.h"
#include "hwinfo.h"
#include "netcfg.h"
#include "bsp_led.h"
#include "bsp_pcf8574.h"
#include "bsp_eeprom_24c02.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- configuration ---- */
#define SHELL_TASK_PRIO    2
#define SHELL_TASK_STACK   (1024)
#define SHELL_LINE_MAX     64
#define SHELL_HISTORY_NUM  3          /* keep last 3 commands */
#define SHELL_PROMPT       "\r\nSTM32> "

/* ---- static state ---- */
static char g_history[SHELL_HISTORY_NUM][SHELL_LINE_MAX];
static uint8_t g_hist_count = 0;
static uint8_t g_hist_head = 0;            /* next write slot */

/* ---- output sink: UART ---- */
static void uart_out(const char *s)
{
  uart_puts(s);
}

void shell_println(shell_out_fn out, const char *s)
{
  out(s);
  out("\r\n");
}

/* ---- history ---- */
static void history_push(const char *line)
{
  if (line[0] == '\0') return;             /* ignore empty */
  /* Copy with explicit NUL termination (avoids -Wstringop-truncation on GCC15
   * when line is exactly SHELL_LINE_MAX-1 bytes). */
  size_t n = strlen(line);
  if (n > SHELL_LINE_MAX - 1) n = SHELL_LINE_MAX - 1;
  memcpy(g_history[g_hist_head], line, n);
  g_history[g_hist_head][n] = '\0';
  g_hist_head = (uint8_t)((g_hist_head + 1) % SHELL_HISTORY_NUM);
  if (g_hist_count < SHELL_HISTORY_NUM) g_hist_count++;
}

/* ---- command implementations ---- */
static void cmd_hw(shell_out_fn out)
{
  hwinfo_static_t s;
  hwinfo_static_copy(&s);
  char buf[48];
  shell_println(out, "=== System ===");
  snprintf(buf, sizeof(buf), "MCU      : %s", s.mcu);      shell_println(out, buf);
  snprintf(buf, sizeof(buf), "Clock    : %s", s.clock);    shell_println(out, buf);
  snprintf(buf, sizeof(buf), "FreeRTOS : %lu tasks", (unsigned long)s.freertos_tasks);
  shell_println(out, buf);
}

static void cmd_dev(shell_out_fn out)
{
  hwinfo_dynamic_t d;
  hwinfo_dynamic_copy(&d);
  char buf[48];
  shell_println(out, "=== Devices ===");
  snprintf(buf, sizeof(buf), "sensor_valid : %u", d.sensor_valid);
  shell_println(out, buf);
  snprintf(buf, sizeof(buf), "i2c_recover  : %lu", (unsigned long)d.i2c_recover);
  shell_println(out, buf);
  snprintf(buf, sizeof(buf), "AP3216C lux/ps/ir : %u / %u / %u",
           d.lux, d.ps, d.ir);
  shell_println(out, buf);
  snprintf(buf, sizeof(buf), "MPU9250 ax/ay/az : %.2f / %.2f / %.2f",
           (double)d.ax, (double)d.ay, (double)d.az);
  shell_println(out, buf);
  snprintf(buf, sizeof(buf), "MPU9250 gx/gy/gz : %.2f / %.2f / %.2f",
           (double)d.gx, (double)d.gy, (double)d.gz);
  shell_println(out, buf);
  snprintf(buf, sizeof(buf), "MPU9250 mx/my/mz : %.2f / %.2f / %.2f",
           (double)d.mx, (double)d.my, (double)d.mz);
  shell_println(out, buf);
  snprintf(buf, sizeof(buf), "LED  : %s", d.led_on ? "ON" : "OFF");
  shell_println(out, buf);
  snprintf(buf, sizeof(buf), "BEEP : %s", d.beep_on ? "ON" : "OFF");
  shell_println(out, buf);
}

/* ---- field validators (MCU side: precise format check, no PCRE) ---- */

/* IPv4 "a.b.c.d" with each octet 0..255. Returns 1 if valid. */
static int valid_ip(const char *s)
{
  int a, b, c, d;
  char extra;
  if (sscanf(s, "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4) return 0;
  if (a < 0 || a > 255 || b < 0 || b > 255 ||
      c < 0 || c > 255 || d < 0 || d > 255) return 0;
  return 1;
}

/* IPv4 netmask: contiguous 1s then 0s. Returns 1 if valid. */
static int valid_mask(const char *s)
{
  if (!valid_ip(s)) return 0;
  uint32_t v = 0;
  int a, b, c, d;
  sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d);
  v = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
  /* valid mask <=> (v + ~v) has no carry hole: (v & (~v + 1)) == 0 */
  return ((v & (~v + 1U)) == 0U);
}

/* MAC "XX:XX:XX:XX:XX:XX" with each byte 00..FF hex. Returns 1 if valid. */
static int valid_mac(const char *s)
{
  unsigned int h[6];
  char extra;
  if (sscanf(s, "%02x:%02x:%02x:%02x:%02x:%02x%c",
             &h[0], &h[1], &h[2], &h[3], &h[4], &h[5], &extra) != 6) return 0;
  for (int i = 0; i < 6; i++)
    if (h[i] > 255) return 0;
  return 1;
}

static void cmd_net(shell_out_fn out, const char *arg)
{
  char buf[64];

  /* no subcommand -> show pending (EEPROM-backed) values */
  if (arg[0] == '\0')
  {
    shell_println(out, "=== Network (pending, applied after reboot) ===");
    snprintf(buf, sizeof(buf), "IP  : %s", g_netcfg.ip);    shell_println(out, buf);
    snprintf(buf, sizeof(buf), "Mask: %s", g_netcfg.mask);  shell_println(out, buf);
    snprintf(buf, sizeof(buf), "GW  : %s", g_netcfg.gw);    shell_println(out, buf);
    snprintf(buf, sizeof(buf), "MAC : %s", g_netcfg.mac);   shell_println(out, buf);
    return;
  }

  /* split subcommand and value */
  char sub[16];
  const char *val = arg;
  int i = 0;
  while (*val && *val != ' ' && *val != '\t' && i < (int)sizeof(sub) - 1)
    sub[i++] = *val++;
  sub[i] = '\0';
  while (*val == ' ' || *val == '\t') val++;
  if (*val == '\0') val = "";   /* no value given */

  if (strcmp(sub, "ip") == 0)
  {
    if (!valid_ip(val)) { shell_println(out, "ERR: bad IP format (expect a.b.c.d, 0-255)"); return; }
    strncpy(g_netcfg.ip, val, sizeof(g_netcfg.ip) - 1);
    g_netcfg.ip[sizeof(g_netcfg.ip) - 1] = '\0';
  }
  else if (strcmp(sub, "mask") == 0)
  {
    if (!valid_mask(val)) { shell_println(out, "ERR: bad MASK format (contiguous netmask)"); return; }
    strncpy(g_netcfg.mask, val, sizeof(g_netcfg.mask) - 1);
    g_netcfg.mask[sizeof(g_netcfg.mask) - 1] = '\0';
  }
  else if (strcmp(sub, "gw") == 0)
  {
    if (!valid_ip(val)) { shell_println(out, "ERR: bad GW format (expect a.b.c.d, 0-255)"); return; }
    strncpy(g_netcfg.gw, val, sizeof(g_netcfg.gw) - 1);
    g_netcfg.gw[sizeof(g_netcfg.gw) - 1] = '\0';
  }
  else if (strcmp(sub, "mac") == 0)
  {
    if (strcmp(val, "random") == 0)
    {
      /* locally-administered unicast random MAC */
      uint8_t r[6];
      for (int k = 0; k < 6; k++) r[k] = (uint8_t)(rand() & 0xFF);
      r[0] = (uint8_t)((r[0] | 0x02U) & 0xFEU);   /* set LAA bit, clear multicast */
      snprintf(g_netcfg.mac, sizeof(g_netcfg.mac),
               "%02X:%02X:%02X:%02X:%02X:%02X",
               r[0], r[1], r[2], r[3], r[4], r[5]);
    }
    else if (valid_mac(val))
    {
      strncpy(g_netcfg.mac, val, sizeof(g_netcfg.mac) - 1);
      g_netcfg.mac[sizeof(g_netcfg.mac) - 1] = '\0';
      /* normalize to upper-case hex (as validated) */
      for (int k = 0; k < (int)strlen(g_netcfg.mac); k++)
        if (g_netcfg.mac[k] >= 'a' && g_netcfg.mac[k] <= 'f')
          g_netcfg.mac[k] = (char)(g_netcfg.mac[k] - ('a' - 'A'));
    }
    else
    {
      shell_println(out, "ERR: bad MAC (expect XX:XX:XX:XX:XX:XX or 'random')");
      return;
    }
  }
  else
  {
    shell_println(out, "ERR: usage: net <ip|mask|gw|mac> <value>");
    return;
  }

  /* persist to EEPROM (effective after reboot) */
  if (netcfg_save(&g_netcfg))
    shell_println(out, "OK: saved, reboot to apply");
  else
    shell_println(out, "ERR: EEPROM write failed");
}

static void cmd_version(shell_out_fn out)
{
  shell_println(out, "Firmware: STM32F429 Net Demo v1.0.0");
  shell_println(out, "Build  : " __DATE__ " " __TIME__);
}

static void cmd_help(shell_out_fn out)
{
  shell_println(out, "Commands:");
  shell_println(out, "  hw               system info (FreeRTOS / MCU / clock)");
  shell_println(out, "  dev              device sensors (AP3216C / MPU9250 / IO)");
  shell_println(out, "  net              network config (no arg=show; net ip/mask/gw/mac <val>)");
  shell_println(out, "  version          firmware version");
  shell_println(out, "  beep on|off      control BEEP hardware");
  shell_println(out, "  led on|off       control LED (PB0, non-heartbeat)");
  shell_println(out, "  reboot           reset MCU (apply EEPROM netcfg)");
  shell_println(out, "  history          show last 3 commands");
  shell_println(out, "  help             this help");
}

static void cmd_history(shell_out_fn out)
{
  shell_println(out, "=== History ===");
  if (g_hist_count == 0) { shell_println(out, "  (empty)"); return; }
  /* oldest -> newest */
  uint8_t start = (g_hist_count < SHELL_HISTORY_NUM)
                  ? 0 : g_hist_head;
  for (uint8_t i = 0; i < g_hist_count; i++)
  {
    uint8_t idx = (uint8_t)((start + i) % SHELL_HISTORY_NUM);
    char buf[144];
    snprintf(buf, sizeof(buf), "  %u: %s", i + 1, g_history[idx]);
    shell_println(out, buf);
  }
}

static void cmd_beep(shell_out_fn out, const char *arg)
{
  if (strcmp(arg, "on") == 0)       { hwinfo_set_beep(1); shell_println(out, "BEEP ON"); }
  else if (strcmp(arg, "off") == 0) { hwinfo_set_beep(0); shell_println(out, "BEEP OFF"); }
  else shell_println(out, "Usage: beep on|off");
}

static void cmd_led(shell_out_fn out, const char *arg)
{
  if (strcmp(arg, "on") == 0)       { hwinfo_set_led(1); shell_println(out, "LED ON (PB0)"); }
  else if (strcmp(arg, "off") == 0) { hwinfo_set_led(0); shell_println(out, "LED OFF (PB0)"); }
  else shell_println(out, "Usage: led on|off");
}

/* Reboot the MCU via the NVIC SYSRESETREQ. Equivalent to a hardware reset:
 * re-runs the boot path, so any EEPROM-backed netcfg changes take effect. */
static void cmd_reboot(shell_out_fn out)
{
  shell_println(out, "Rebooting...");
  /* out() above pushes through the UART ring buffer; make sure the line is
   * actually drained onto the wire before we reset. */
  uart_flush();
  NVIC_SystemReset();
  /* does not return */
}

/* ---- public parser (transport-agnostic) ---- */
int shell_exec(const char *line, shell_out_fn out)
{
  char buf[SHELL_LINE_MAX];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  /* trim leading spaces */
  char *p = buf;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == '\0') return 0;                 /* empty line, ok */

  /* split command and argument */
  char *cmd = p;
  char *sp = p;
  while (*sp && *sp != ' ' && *sp != '\t') sp++;
  char *arg = sp;
  if (*sp) { *sp = '\0'; arg++; while (*arg == ' ' || *arg == '\t') arg++; }

  if      (strcmp(cmd, "hw") == 0)      cmd_hw(out);
  else if (strcmp(cmd, "dev") == 0)     cmd_dev(out);
  else if (strcmp(cmd, "net") == 0)     cmd_net(out, arg);
  else if (strcmp(cmd, "version") == 0) cmd_version(out);
  else if (strcmp(cmd, "help") == 0)    cmd_help(out);
  else if (strcmp(cmd, "history") == 0) cmd_history(out);
  else if (strcmp(cmd, "beep") == 0)    cmd_beep(out, arg);
  else if (strcmp(cmd, "led") == 0)     cmd_led(out, arg);
  else if (strcmp(cmd, "reboot") == 0)  cmd_reboot(out);
  else
  {
    char e[144];
    snprintf(e, sizeof(e), "Unknown command: %s", cmd);
    shell_println(out, e);
    return -1;
  }
  return 0;
}

/* ---- one complete line: record history then execute ----
 * Extracted from shell_task so the same history+exec path is shared by the
 * UART shell and any future transport (telnet). 'out' is the output sink,
 * so the caller decides where results go (uart_out for the console,
 * telnet_out for a Telnet session). */
void shell_feed_line_ex(const char *line, shell_out_fn out)
{
  if (line[0] != '\0') history_push(line);
  shell_exec(line, out);
}

/* Backwards-compatible helper: route output to the UART console. */
void shell_feed_line(const char *line)
{
  shell_feed_line_ex(line, uart_out);
}

/* ---- shell task: read RX, echo, build line, push to queue ---- */
static void shell_task(void *arg)
{
  (void)arg;
  char line[SHELL_LINE_MAX];
  int  len = 0;
  uint8_t c;
  uint8_t esc_state = 0;   /* 0: normal  1: got ESC(0x1B)  2: got ESC[ */

  uart_puts("\r\n=== STM32F429 Shell ===\r\nType 'help' for commands.\r\n");
  uart_puts(SHELL_PROMPT);

  for (;;)
  {
    if (uart_getc(&c))
    {
      /* ---- ANSI escape sequence (cursor / arrow keys) ----
       * Arrow keys emit ESC [ A/B/C/D. We discard the whole sequence
       * silently: no echo, no store. Keep the line buffer intact. */
      if (esc_state == 1)
      {
        if (c == '[') esc_state = 2;     /* CSI introducer, wait for final */
        else esc_state = 0;              /* not a sequence we handle */
        continue;
      }
      if (esc_state == 2)
      {
        esc_state = 0;                   /* final byte: drop, done */
        continue;
      }
      if (c == 0x1B)                     /* start of escape sequence */
      {
        esc_state = 1;
        continue;
      }

      /* handle editing keys */
      if (c == '\r' || c == '\n')
      {
        uart_puts("\r\n");
        if (len > 0)
        {
          line[len] = '\0';
          shell_feed_line(line);
          len = 0;
        }
        uart_puts(SHELL_PROMPT);
      }
      else if (c == 0x08 || c == 0x7F)   /* BS / DEL */
      {
        if (len > 0)
        {
          len--;
          uart_puts("\b \b");            /* erase on terminal */
        }
      }
      else if (len < SHELL_LINE_MAX - 1)
      {
        line[len++] = (char)c;
        uart_write(&c, 1);               /* echo single byte (no strlen) */
      }
      /* else: line full, drop */
    }
    else
    {
      vTaskDelay(pdMS_TO_TICKS(5));      /* idle poll */
    }
  }
}

void shell_init(void)
{
  if (xTaskCreate(shell_task, "shell", SHELL_TASK_STACK, NULL,
                  SHELL_TASK_PRIO, NULL) != pdPASS)
  {
    /* best effort: led stays heartbeat, shell just won't start */
  }
}
