/**
  ******************************************************************************
  * @file    telnet_shell.h
  * @brief   Telnet server that exposes the same command-line shell as the
  *          debug UART, over TCP port 23.
  *
  *          One server task accepts connections and spawns one dedicated
  *          task per client. Each client runs independently and concurrently
  *          with the UART shell and with other Telnet clients.
  *
  *          Output is routed per-connection: the shell's shell_out_fn sink
  *          writes to that client's netconn, so a command typed on Telnet
  *          only echoes back to that Telnet session (never to the UART or to
  *          other clients).
  ******************************************************************************
  */
#ifndef __TELNET_SHELL_H__
#define __TELNET_SHELL_H__

#include <stdint.h>

/* Standard Telnet port. */
#define TELNET_PORT         23

/* Maximum number of simultaneous client connections. The server rejects
 * further connections (closing them immediately) once this many are active,
 * to bound netconn/memory usage. */
#define TELNET_MAX_CONNS    3

/* Stack size (words) for the per-connection task. Must hold the shell
 * command handlers' local buffers plus the netconn recv path. */
#define TELNET_CONN_STACK   (1024)

/* Priority: same as the UART shell task so neither starves the other. */
#define TELNET_CONN_PRIO    (2)

/* Start the Telnet server (creates the listener task). Call once after the
 * scheduler is up and the network is initialized. Safe to call before
 * vTaskStartScheduler() (matches the http/https server init pattern). */
void telnet_shell_init(void);

#endif /* __TELNET_SHELL_H__ */
