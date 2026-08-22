/**
  ******************************************************************************
  * @file    shell.h
  * @brief   Command-line shell over the debug UART.
  *
  *          - RX: interrupt -> ring buffer; a dedicated shell_task reads bytes,
  *            echoes them, builds a line, and on <CR>/<LF> pushes the line to a
  *            command queue.
  *          - Parsing runs in shell_task (single thread) so the same
  *            shell_exec() can later be driven by telnet without changes.
  *          - TX: thread-safe via uart_puts() (mutex-protected ring buffer).
  *
  *          Commands: hw / dev / net / version / beep on|off / led on|off /
  *                     help / history
  ******************************************************************************
  */
#ifndef __SHELL_H__
#define __SHELL_H__

#include <stdint.h>

/* Output sink used by shell_exec so the same parser serves UART and telnet. */
typedef void (*shell_out_fn)(const char *s);

/* Start the shell: create the shell_task and the RX/command plumbing.
 * Call once after the scheduler is up (UART must be initialized first). */
void shell_init(void);

/* Parse and execute one command line. 'out' receives all output lines.
 * Returns 0 on success, <0 on unknown command. Safe to call from any context
 * provided 'out' and the line buffer are stable for the call duration. */
int shell_exec(const char *line, shell_out_fn out);

/* Feed one complete line (history + execute). Shared by the UART shell and any
 * future transport. Empty lines are ignored (not recorded in history). */
void shell_feed_line(const char *line);

/* Convenience: print one string + CRLF via the given sink. */
void shell_println(shell_out_fn out, const char *s);

#endif /* __SHELL_H__ */
