/**
  ******************************************************************************
  * @file    telnet_shell.c
  * @brief   Telnet server (port 23) exposing the shared command-line shell.
  *
  *          Architecture:
  *            telnet_shell_init()  -> creates the listener task
  *            telnet_server_task() -> netconn_new/bind/listen, then loops
  *                                    netconn_accept, spawning one task per
  *                                    accepted client.
  *            telnet_conn_task()   -> per-client: IAC negotiation, line edit
  *                                    + echo, CR/LF runs shell_feed_line().
  *
  *          Concurrency: the listener and every client run as independent
  *          FreeRTOS tasks. A global (mutex-guarded) counter bounds the
  *          number of simultaneous clients to TELNET_MAX_CONNS. The UART
  *          shell task is untouched and runs in parallel.
  *
  *          Resource release: on any disconnect path (client close, recv
  *          error/timeout, or max-conns exceeded) telnet_conn_cleanup()
  *          runs netconn_close + netconn_delete, decrements the counter, and
  *          deletes the calling task. This guarantees no leaked netconns or
  *          orphaned tasks.
  ******************************************************************************
  */
#include "telnet_shell.h"
#include "shell.h"
#include "log.h"

#include "lwip/api.h"
#include "lwip/ip_addr.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <string.h>
#include <stdio.h>

/* Active connection used by telnet_out(). Set before shell_exec, cleared
 * after. Each client is its own task, so this global is only ever touched by
 * the owning task during its own shell_exec call. Declared early so
 * telnet_out() (defined below) can see it. */
struct netconn *telnet_active_conn = NULL;

/* ---- Telnet protocol constants (RFC 854) ---- */
#define TELNET_IAC   255     /* Interpret As Command */
#define TELNET_DONT  254
#define TELNET_DO    253
#define TELNET_WONT  252
#define TELNET_WILL  251
#define TELNET_SE    240
#define TELNET_NOP   241
#define TELNET_SB    250

#define TELNET_OPT_ECHO      1
#define TELNET_OPT_SGA       3     /* Suppress Go-Ahead */

/* ---- line buffer ---- */
#define TELNET_LINE_MAX   64
/* No leading \r\n: it stacks on the trailing \r\n of the last shell_println
 * and creates a phantom blank line. The CRLF handling in the recv loop is
 * responsible for moving the cursor to a new line; the prompt itself just
 * prints "STM32> " at the current column. */
#define TELNET_PROMPT     "STM32> "

/* ---- idle disconnect ---- */
#define TELNET_IDLE_MS    (30U * 1000U)
/* netconn_set_recvtimeout uses ms. We poll-check each second against
 * last_active so the disconnect is detected within 1s of expiry. */
#define TELNET_POLL_MS    (1000U)

/* ---- shared connection state ---- */
static SemaphoreHandle_t s_conn_mutex = NULL;
static uint8_t           g_conn_count = 0;   /* active client tasks */

/* forward */
static void telnet_conn_task(void *arg);
static void telnet_conn_cleanup(struct netconn *conn);

/* ---- per-connection output sink for shell_exec ---- */
typedef struct
{
  struct netconn *conn;
} telnet_out_ctx_t;

static int telnet_send(struct netconn *conn, const void *data, int len)
{
  const unsigned char *p = (const unsigned char *)data;
  int total = (len < 0) ? (int)strlen((const char *)data) : len;
  int remain = total;
  while (remain > 0)
  {
    int chunk = (remain > 1024) ? 1024 : remain;
    err_t err = netconn_write(conn, p, (u16_t)chunk, NETCONN_COPY);
    if (err == ERR_OK)
    {
      p += chunk;
      remain -= chunk;
    }
    else if (err == ERR_MEM)
    {
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    else
    {
      return -1;   /* connection broken */
    }
  }
  return 0;
}

static void telnet_out(const char *s)
{
  /* shell_exec calls out() with only the string; the active connection is
   * kept in telnet_active_conn (set just before shell_exec, cleared after).
   * Each client is its own task, so this global is only ever touched by the
   * owning task during its own shell_exec call. */
  if (telnet_active_conn == NULL) return;
  telnet_send(telnet_active_conn, s, (int)strlen(s));
}

/* ---- IAC negotiation: standard RFC854 minimal responder ----
 * Only ECHO(1) and SGA(3) are enabled on our side. For any client request
 * we reply with the symmetric, correct option verb:
 *   client WILL/WONT <opt>  -> we DONT <opt>   (peer's own choice, we don't care)
 *   client DO   <opt>       -> we WILL <opt>   if we support it, else WONT
 *   client DONT <opt>       -> we WONT <opt>
 * Sub-negotiation (SB ... IAC SE) is consumed and ignored. Escaped 255
 * (IAC IAC) is treated as a literal data byte 255 by the caller.
 * All replies are non-blocking writes sent straight back on the same conn. */
static void telnet_reply_option(struct netconn *conn, uint8_t cmd, uint8_t opt)
{
  uint8_t reply[3];
  reply[0] = TELNET_IAC;
  if (cmd == TELNET_WILL || cmd == TELNET_WONT)
  {
    /* Peer declares its own intent; we simply decline to require it. */
    reply[1] = TELNET_DONT;
    reply[2] = opt;
  }
  else if (cmd == TELNET_DO)
  {
    /* Peer asks US to enable opt: agree only for ECHO/SGA. */
    reply[1] = (opt == TELNET_OPT_ECHO || opt == TELNET_OPT_SGA)
               ? TELNET_WILL : TELNET_WONT;
    reply[2] = opt;
  }
  else /* DONT */
  {
    reply[1] = TELNET_WONT;
    reply[2] = opt;
  }
  netconn_write(conn, reply, 3, NETCONN_COPY);
}

/* Process a complete, already-delimited IAC command stream (NO data bytes).
 * Used by the recv loop once a full command/sub-negotiation has been
 * reassembled from the byte stream. */
static void telnet_handle_iac(struct netconn *conn, const uint8_t *buf, int len)
{
  int i = 0;
  while (i < len)
  {
    if (buf[i] != TELNET_IAC) { i++; continue; }
    if (i + 1 >= len) break;                 /* truncated, wait for more */
    uint8_t c = buf[i + 1];
    if (c == TELNET_IAC) { i += 2; continue; }   /* escaped 255, ignore here */
    if (c == TELNET_SB)
    {
      /* Sub-negotiation: skip until IAC SE. */
      int j = i + 2;
      while (j + 1 < len)
      {
        if (buf[j] == TELNET_IAC && buf[j + 1] == TELNET_SE) { j += 2; break; }
        j++;
      }
      i = j;
      continue;
    }
    if (c == TELNET_WILL || c == TELNET_WONT ||
        c == TELNET_DO   || c == TELNET_DONT)
    {
      if (i + 2 >= len) break;
      uint8_t opt = buf[i + 2];
      telnet_reply_option(conn, c, opt);
      i += 3;
    }
    else
    {
      i += 2;   /* unrecognized 2-byte command: skip */
    }
  }
}

/* ---- per-client task ---- */
static void telnet_conn_task(void *arg)
{
  struct netconn *conn = (struct netconn *)arg;
  char line[TELNET_LINE_MAX];
  int  len = 0;
  uint8_t c;

  /* IAC state machine MUST be per-task (not static) so concurrent clients
   * do not corrupt each other's negotiation state.
   *   iac_state: 0 normal, 1 IAC seen, 2 SB subnego, 3 opt pending, 4 SE? */
  uint8_t iac_state = 0;
  uint8_t iac_verb  = 0;

  /* CRLF handling: the wire delivers CR LF as two bytes that both enter the
   * normal-data path. Process the first (which triggers command execution
   * and emits the prompt), and absorb the second so it does not produce
   * a spurious second prompt on an empty line. */
  uint8_t cr_pending = 0;

  /* Idle-disconnect tracking: refresh on every received byte or executed
   * command; if it ages past TELNET_IDLE_MS without activity we tear down. */
  TickType_t last_active = xTaskGetTickCount();

  /* Enable per-recv timeout so we can poll-check idle. 0 would mean BLOCK
   * forever — bad because we need to enforce the 30 s timeout. */
  netconn_set_recvtimeout(conn, TELNET_POLL_MS);

  /* ---- Active negotiation: declare ECHO+SGA BEFORE sending any banner
   * bytes. This puts the client (PuTTY/teraterm) into the expected
   * Suppress-Go-Ahead echo state immediately, so subsequent raw output
   * (banner, prompts) is not misinterpreted as negotiation noise. The
   * client will refuse any options it doesn't like; replies come in on
   * the recv loop and are handled in telnet_handle_iac(). */
  {
    static const uint8_t negotiate[] = {
      TELNET_IAC, TELNET_WILL, TELNET_OPT_ECHO,
      TELNET_IAC, TELNET_WILL, TELNET_OPT_SGA,
      TELNET_IAC, TELNET_DO,   TELNET_OPT_SGA,
    };
    netconn_write(conn, negotiate, sizeof(negotiate), NETCONN_COPY);
    /* Allow the peer a moment to absorb / respond before we start streaming
     * banner text. */
    vTaskDelay(pdMS_TO_TICKS(120));
  }

  /* Welcome banner identical to the UART shell. */
  telnet_send(conn,
      "\r\n=== STM32F429 Shell (telnet) ===\r\n"
      "Type 'help' for commands, 'exit' to disconnect.\r\n", -1);
  telnet_send(conn, TELNET_PROMPT, -1);

  for (;;)
  {
    /* ---- idle-timeout check (before blocking recv would block too long) ---- */
    if ((xTaskGetTickCount() - last_active) > pdMS_TO_TICKS(TELNET_IDLE_MS))
    {
      telnet_send(conn, "\r\n[timeout] idle, closing.\r\n", -1);
      break;   /* -> cleanup */
    }

    struct netbuf *nb = NULL;
    err_t err = netconn_recv(conn, &nb);
    if (err == ERR_TIMEOUT)
    {
      continue;   /* poll; loop back to re-check idle */
    }
    if (err != ERR_OK || nb == NULL)
    {
      /* Peer closed or fatal error. */
      break;
    }

    last_active = xTaskGetTickCount();   /* any byte = activity */

    void *data;
    u16_t n;
    if (netbuf_data(nb, &data, &n) != ERR_OK || n == 0)
    {
      netbuf_delete(nb);
      continue;
    }

    const uint8_t *p = (const uint8_t *)data;
    for (u16_t k = 0; k < n; ++k)
    {
      c = p[k];

      /* ---- Telnet IAC command processing ----
       * State machine (per-task locals, never static):
       *   0 = normal data
       *   1 = saw IAC, waiting for the command verb (WILL/WONT/DO/DONT/SB/SE)
       *   2 = inside sub-negotiation (SB ... IAC SE)
       * Escaped 255 (IAC IAC) is emitted as a literal data byte 255.
       * IAC bytes NEVER reach the line editor. */
      if (iac_state == 1)
      {
        if (c == TELNET_IAC)
        {
          /* IAC IAC -> literal 255 data byte. Emit it as a normal
           * character (stored in the line buffer + echoed), then resume
           * normal data parsing. */
          iac_state = 0;
          if (len < TELNET_LINE_MAX - 1)
          {
            line[len++] = (char)255;
            telnet_send(conn, &c, 1);
          }
          continue;
        }
        else if (c == TELNET_SB)
        {
          iac_state = 2;          /* begin sub-negotiation */
          continue;
        }
        else if (c == TELNET_WILL || c == TELNET_WONT ||
                 c == TELNET_DO   || c == TELNET_DONT)
        {
          /* 2-byte command; the option follows in the next byte. */
          iac_verb = c;
          iac_state = 3;          /* waiting for option byte */
          continue;
        }
        else
        {
          /* other 2-byte command (e.g. NOP, SE without SB): ignore */
          iac_state = 0;
          continue;
        }
      }
      else if (iac_state == 3)
      {
        /* option byte for the pending WILL/WONT/DO/DONT */
        uint8_t iac_cmd[3];
        iac_cmd[0] = TELNET_IAC;
        iac_cmd[1] = iac_verb;
        iac_cmd[2] = c;
        telnet_handle_iac(conn, iac_cmd, 3);
        iac_state = 0;
        continue;
      }
      else if (iac_state == 2)
      {
        /* sub-negotiation: buffer until IAC SE */
        if (c == TELNET_IAC)
        {
          iac_state = 4;          /* possible SE next */
          continue;
        }
        continue;                 /* swallow sub-nego payload */
      }
      else if (iac_state == 4)
      {
        if (c == TELNET_SE)
        {
          iac_state = 0;          /* end of sub-negotiation */
          continue;
        }
        /* not SE: false alarm, resume sub-nego (an IAC inside SB is rare) */
        iac_state = 2;
        continue;
      }

      if (c == TELNET_IAC)
      {
        iac_state = 1;            /* enter IAC command parsing */
        continue;
      }

      /* ---- CRLF dedup: if the previous byte was CR and this one is LF,
       * drop the LF so it does not run an empty line + duplicate prompt. */
      if (cr_pending)
      {
        cr_pending = 0;
        if (c == '\n') { continue; }
        /* otherwise fall through and process 'c' as a normal byte */
      }

      /* ---- normal input byte ---- */
      if (c == '\r' || c == '\n')
      {
        /* CRLF -> start a fresh line and run any pending command. */
        telnet_send(conn, "\r\n", 2);
        if (c == '\r') cr_pending = 1;   /* eat a following LF, if any */

        if (len > 0)
        {
          line[len] = '\0';

          /* Telnet-only command: 'exit' closes this connection. */
          if (strcmp(line, "exit") == 0)
          {
            telnet_send(conn, "Bye!\r\n", -1);
            netbuf_delete(nb);   /* free the netbuf before cleanup */
            telnet_conn_cleanup(conn);
            /* does not return */
          }

          telnet_active_conn = conn;     /* route shell output to this conn */
          shell_feed_line_ex(line, telnet_out);
          telnet_active_conn = NULL;
          len = 0;
        }
        /* Always finish the line with the prompt. (Even for empty lines:
         * the user gets a fresh "STM32> ".) */
        telnet_send(conn, TELNET_PROMPT, -1);
        last_active = xTaskGetTickCount();
      }
      else if (c == 0x08 || c == 0x7F)   /* BS / DEL */
      {
        if (len > 0)
        {
          len--;
          telnet_send(conn, "\b \b", 3);
        }
      }
      else if (c == 0)
      {
        /* NUL (0) is not a valid line character: some telnet clients send
         * stray NULs in their stream. Writing it into 'line' would terminate
         * the command string early (str* functions stop at '\0'), causing
         * commands like 'help' to be dropped/garbled. Just ignore it. */
        /* (no store, no echo) */
      }
      else if (len < TELNET_LINE_MAX - 1)
      {
        line[len++] = (char)c;
        telnet_send(conn, &c, 1);        /* echo */
      }
      /* else: line full, drop */
    }

    netbuf_delete(nb);
  }

  /* ---- disconnect / error / idle timeout: release everything ---- */
  telnet_conn_cleanup(conn);
  /* does not return */
}

/* ---- release a client connection and self-delete ---- */
static void telnet_conn_cleanup(struct netconn *conn)
{
  /* 1) Drain any residual inbound so the peer's in-flight bytes are
   *    consumed (avoids a RST from the stack on delete). */
  {
    struct netbuf *rb = NULL;
    TickType_t start = xTaskGetTickCount();
    while (netconn_recv(conn, &rb) == ERR_OK)
    {
      if (rb) netbuf_delete(rb);
      if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(300)) break;
    }
  }

  /* 2) Graceful close: send FIN. netconn_close() returns once the FIN is
   *    queued; it does NOT block for the peer's ACK. */
  netconn_close(conn);

  /* 3) Give the tcpip_thread a moment to push any pending output (the
   *    "Bye!" line, the FIN) onto the wire before we delete the PCB.
   *    Without this the stack can emit an RST and the peer sees an
   *    abrupt reset instead of a clean close. */
  vTaskDelay(pdMS_TO_TICKS(200));

  netconn_delete(conn);

  if (s_conn_mutex != NULL)
  {
    xSemaphoreTake(s_conn_mutex, portMAX_DELAY);
    if (g_conn_count > 0) g_conn_count--;
    xSemaphoreGive(s_conn_mutex);
  }

  vTaskDelete(NULL);   /* end this client task */
}

/* ---- server listener task ---- */
static void telnet_server_task(void *arg)
{
  (void)arg;
  struct netconn *listen = NULL;
  struct netconn *newconn = NULL;
  err_t err;

  listen = netconn_new(NETCONN_TCP);
  if (listen == NULL)
  {
    PRINT_LOG("TELNET: netconn_new failed\r\n");
    vTaskDelete(NULL);
    return;
  }

  err = netconn_bind(listen, IP_ADDR_ANY, TELNET_PORT);
  if (err != ERR_OK)
  {
    PRINT_LOG("TELNET: bind failed (%d)\r\n", (int)err);
    netconn_delete(listen);
    vTaskDelete(NULL);
    return;
  }

  err = netconn_listen(listen);
  if (err != ERR_OK)
  {
    PRINT_LOG("TELNET: listen failed (%d)\r\n", (int)err);
    netconn_delete(listen);
    vTaskDelete(NULL);
    return;
  }

  PRINT_LOG("TELNET: listening on port %d\r\n", TELNET_PORT);

  for (;;)
  {
    err = netconn_accept(listen, &newconn);
    if (err != ERR_OK || newconn == NULL)
    {
      continue;
    }

    /* Enforce max concurrent connections. */
    int accept_it = 0;
    if (s_conn_mutex == NULL)
    {
      s_conn_mutex = xSemaphoreCreateMutex();
    }
    if (s_conn_mutex != NULL)
    {
      xSemaphoreTake(s_conn_mutex, portMAX_DELAY);
      if (g_conn_count < TELNET_MAX_CONNS)
      {
        g_conn_count++;
        accept_it = 1;
      }
      xSemaphoreGive(s_conn_mutex);
    }

    if (!accept_it)
    {
      /* Too many clients: refuse politely and release. */
      telnet_send(newconn,
          "\r\nToo many connections, try again later.\r\n", -1);
      netconn_close(newconn);
      netconn_delete(newconn);
      continue;
    }

    /* Spawn a dedicated task for this client. */
    char name[16];
    snprintf(name, sizeof(name), "telnet%d", (int)g_conn_count);
    if (xTaskCreate(telnet_conn_task, name, TELNET_CONN_STACK,
                    (void *)newconn, TELNET_CONN_PRIO, NULL) != pdPASS)
    {
      /* Could not create task: release this slot. */
      if (s_conn_mutex != NULL)
      {
        xSemaphoreTake(s_conn_mutex, portMAX_DELAY);
        if (g_conn_count > 0) g_conn_count--;
        xSemaphoreGive(s_conn_mutex);
      }
      netconn_close(newconn);
      netconn_delete(newconn);
    }
    /* else: telnet_conn_task owns the conn and will clean it up on exit. */
  }
  /* never reached */
}

void telnet_shell_init(void)
{
  if (s_conn_mutex == NULL)
  {
    s_conn_mutex = xSemaphoreCreateMutex();
  }
  if (xTaskCreate(telnet_server_task, "telnetd", 1024, NULL,
                  TELNET_CONN_PRIO, NULL) != pdPASS)
  {
    PRINT_LOG("TELNET: server task create failed\r\n");
  }
}
