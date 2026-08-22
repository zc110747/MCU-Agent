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

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <string.h>
#include <stdio.h>

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
  strncpy(g_history[g_hist_head], line, SHELL_LINE_MAX - 1);
  g_history[g_hist_head][SHELL_LINE_MAX - 1] = '\0';
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

static void cmd_net(shell_out_fn out)
{
  hwinfo_static_t s;
  hwinfo_static_copy(&s);
  char buf[48];
  shell_println(out, "=== Network ===");
  snprintf(buf, sizeof(buf), "IP  : %s", s.ip);    shell_println(out, buf);
  snprintf(buf, sizeof(buf), "Mask: %s", s.mask);  shell_println(out, buf);
  snprintf(buf, sizeof(buf), "GW  : %s", s.gw);    shell_println(out, buf);
  snprintf(buf, sizeof(buf), "MAC : %s", s.mac);   shell_println(out, buf);
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
  shell_println(out, "  net              network config (IP / MASK / GW / MAC)");
  shell_println(out, "  version          firmware version");
  shell_println(out, "  beep on|off      control BEEP hardware");
  shell_println(out, "  led on|off       control LED (PB0, non-heartbeat)");
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
  else if (strcmp(cmd, "net") == 0)     cmd_net(out);
  else if (strcmp(cmd, "version") == 0) cmd_version(out);
  else if (strcmp(cmd, "help") == 0)    cmd_help(out);
  else if (strcmp(cmd, "history") == 0) cmd_history(out);
  else if (strcmp(cmd, "beep") == 0)    cmd_beep(out, arg);
  else if (strcmp(cmd, "led") == 0)     cmd_led(out, arg);
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
 * UART shell and any future transport (telnet). */
void shell_feed_line(const char *line)
{
  if (line[0] != '\0') history_push(line);
  shell_exec(line, uart_out);
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
