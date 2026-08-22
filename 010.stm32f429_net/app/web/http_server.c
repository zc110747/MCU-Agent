/**
  ******************************************************************************
  * @file    http_server.c
  * @brief   Minimal HTTP/1.1 server (LwIP netconn API) in its own FreeRTOS
  *          task, serving the flash page on port 80.
  ******************************************************************************
  */
#include "http_server.h"
#include "lwip/api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "web_page.h"
#include <stdio.h>
#include <string.h>

#define HTTP_SERVER_STACK_SIZE   512
#define HTTP_SERVER_PRIORITY     3

static void http_server_thread(void *arg)
{
  (void)arg;
  struct netconn *conn = NULL;
  struct netconn *newconn = NULL;
  err_t err;

  conn = netconn_new(NETCONN_TCP);
  if (conn == NULL)
  {
    printf("HTTP server: netconn_new failed\r\n");
    vTaskDelete(NULL);
    return;
  }

  err = netconn_bind(conn, IP_ADDR_ANY, 80);
  if (err != ERR_OK)
  {
    printf("HTTP server: bind failed (%d)\r\n", (int)err);
    netconn_delete(conn);
    vTaskDelete(NULL);
    return;
  }

  err = netconn_listen(conn);
  if (err != ERR_OK)
  {
    printf("HTTP server: listen failed (%d)\r\n", (int)err);
    netconn_delete(conn);
    vTaskDelete(NULL);
    return;
  }

  printf("HTTP server: listening on port 80 (netconn task)\r\n");

  for (;;)
  {
    err = netconn_accept(conn, &newconn);
    if (err != ERR_OK)
    {
      continue;
    }

    /* Drain the request first: closing a conn with unread data sends RST. */
    struct netbuf *inbuf = NULL;
    if (netconn_recv(newconn, &inbuf) == ERR_OK && inbuf != NULL)
    {
      netbuf_delete(inbuf);
    }

    /* Reply with the flash page */
    char header[160];
    int page_len = (int)strlen(HTTP_PAGE);
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", page_len);

    if (netconn_write(newconn, header, (u16_t)header_len, NETCONN_COPY) == ERR_OK)
    {
      netconn_write(newconn, HTTP_PAGE, (u16_t)page_len, NETCONN_COPY);
    }

    netconn_close(newconn);
    netconn_delete(newconn);
  }
}

void http_server_init(void)
{
  if (xTaskCreate(http_server_thread, "httpd", HTTP_SERVER_STACK_SIZE, NULL,
                  HTTP_SERVER_PRIORITY, NULL) != pdPASS)
  {
    printf("HTTP server: task create failed\r\n");
  }
}
