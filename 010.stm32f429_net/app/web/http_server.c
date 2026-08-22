/**
  ******************************************************************************
  * @file    http_server.c
  * @brief   HTTP server (port 80) in a FreeRTOS task (netconn API).
  *
  *          Pages come from the SD card (web/ dir) via web_serve(); the
  *          built-in flash page is the fallback. JSON /api endpoints are
  *          handled by web_serve() too.
  ******************************************************************************
  */
#include "stm32f4xx_hal.h"
#include "lwip/api.h"
#include "web_serve.h"
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"
#include <string.h>

/* case-insensitive substring search (newlib-nano lacks strcasestr) */
static const char *stristr(const char *hay, const char *needle)
{
  int nlen = (int)strlen(needle);
  if (nlen == 0) return hay;
  for (; *hay; ++hay)
  {
    int i = 0;
    for (; needle[i] && hay[i]; ++i)
    {
      char a = hay[i], b = needle[i];
      if (a >= 'A' && a <= 'Z') a += 32;
      if (b >= 'A' && b <= 'Z') b += 32;
      if (a != b) break;
    }
    if (i == nlen) return hay;
  }
  return NULL;
}

#define HTTP_PORT          80
#define HTTP_SERVER_STACK  2048   /* web_serve + api_hardware locals need room */
#define HTTP_SERVER_PRIO   3
#define REQ_BUF            2048

static int http_send(void *ctx, const void *data, int len)
{
  struct netconn *conn = (struct netconn *)ctx;
  const unsigned char *p = (const unsigned char *)data;
  int remain = len;
  /* netconn_write(NETCONN_COPY) may fail with ERR_MEM when the pbuf pool is
   * short on memory; a single call can also flush only part of a large
   * response (e.g. the 72 KB Vue bundle). Retry with a backoff so the whole
   * payload reaches the peer. */
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
      return -1;
    }
  }
  return 0;
}

static void http_server_thread(void *arg)
{
  struct netconn *conn = NULL;
  struct netconn *newconn = NULL;
  err_t err;
  (void)arg;

  conn = netconn_new(NETCONN_TCP);
  if (conn == NULL)
  {
    PRINT_LOG("HTTP server: netconn_new failed\r\n");
    vTaskDelete(NULL);
    return;
  }

  err = netconn_bind(conn, IP_ADDR_ANY, HTTP_PORT);
  if (err != ERR_OK)
  {
    PRINT_LOG("HTTP server: bind failed (%d)\r\n", (int)err);
    netconn_delete(conn);
    vTaskDelete(NULL);
    return;
  }

  err = netconn_listen(conn);
  if (err != ERR_OK)
  {
    PRINT_LOG("HTTP server: listen failed (%d)\r\n", (int)err);
    netconn_delete(conn);
    vTaskDelete(NULL);
    return;
  }

  PRINT_LOG("HTTP server: listening on port %d (netconn task)\r\n", HTTP_PORT);

  for (;;)
  {
    err = netconn_accept(conn, &newconn);
    if (err != ERR_OK)
    {
      continue;
    }

    /* read the request (start line + headers + body) */
    char req[REQ_BUF];
    int reqlen = 0;
    struct netbuf *nb = NULL;
    int header_done = 0;
    int body_need = 0;      /* expected body bytes (0 for GET/HEAD) */
    int body_got = 0;
    while (reqlen < (int)sizeof(req) - 1 &&
           netconn_recv(newconn, &nb) == ERR_OK && nb != NULL)
    {
      void *data;
      u16_t len;
      if (netbuf_data(nb, &data, &len) == ERR_OK && len > 0)
      {
        int room = (int)sizeof(req) - 1 - reqlen;
        int take = (len < room) ? (int)len : room;
        memcpy(req + reqlen, data, (size_t)take);
        reqlen += take;

        if (!header_done)
        {
          /* look for end of headers */
          const char *he = stristr(req, "\r\n\r\n");
          if (he != NULL)
          {
            header_done = 1;
            int hend = (int)(he - req) + 4;
            const char *cl = stristr(req, "content-length:");
            body_need = (cl != NULL) ? atoi(cl + 15) : 0;
            body_got = reqlen - hend;
            if (body_got >= body_need)
            {
              netbuf_delete(nb);
              break;
            }
          }
        }
        else
        {
          /* header already seen; keep accumulating body */
          const char *he = stristr(req, "\r\n\r\n");
          int hend = (he != NULL) ? (int)(he - req) + 4 : 0;
          body_got = reqlen - hend;
          if (body_got >= body_need)
          {
            netbuf_delete(nb);
            break;
          }
        }
      }
      netbuf_delete(nb);
    }
    req[reqlen] = '\0';

    if (reqlen > 0)
    {
      web_serve(newconn, http_send, newconn, req, reqlen, 0);
    }

    /* Graceful close: send FIN, then drain until the peer closes (or a
     * short timeout) so queued TX data is not aborted by netconn_delete.
     * Without this, netconn_delete can RST the connection mid-transfer,
     * which browsers surface as "connection reset". */
    netconn_close(newconn);
    {
      struct netbuf *rb = NULL;
      TickType_t start = xTaskGetTickCount();
      while (netconn_recv(newconn, &rb) == ERR_OK)
      {
        if (rb) netbuf_delete(rb);
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(2000)) break;
      }
    }
    netconn_delete(newconn);
  }
}

void http_server_init(void)
{
  if (xTaskCreate(http_server_thread, "httpd", HTTP_SERVER_STACK, NULL,
                  HTTP_SERVER_PRIO, NULL) != pdPASS)
  {
    PRINT_LOG("HTTP server: task create failed\r\n");
  }
}
