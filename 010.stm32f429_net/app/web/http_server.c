/**
  ******************************************************************************
  * @file    http_server.c
  * @brief   Minimal HTTP/1.1 server (LwIP raw API) serving a flash page.
  ******************************************************************************
  */
#include "http_server.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include <string.h>
#include <stdio.h>
#include "web_page.h"

static struct tcp_pcb *http_pcb = NULL;

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t err);

static void http_send(struct tcp_pcb *pcb)
{
  char header[160];
  int header_len;
  int page_len = (int)strlen(HTTP_PAGE);

  header_len = snprintf(header, sizeof(header),
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Content-Length: %d\r\n"
      "Connection: close\r\n"
      "\r\n", page_len);

  /* COPY flag: the stack copies the data, so we may reuse/free our buffers */
  if (tcp_write(pcb, header, (u16_t)header_len, TCP_WRITE_FLAG_COPY) == ERR_OK)
  {
    tcp_write(pcb, HTTP_PAGE, (u16_t)page_len, TCP_WRITE_FLAG_COPY);
  }
  tcp_output(pcb);
  tcp_close(pcb);
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
  (void)arg;

  if (err != ERR_OK)
  {
    if (p != NULL)
    {
      pbuf_free(p);
    }
    return ERR_OK;
  }

  /* Connection closed by the client */
  if (p == NULL)
  {
    tcp_close(pcb);
    return ERR_OK;
  }

  /* We don't parse the request here; just acknowledge and send the page */
  tcp_recved(pcb, p->tot_len);
  pbuf_free(p);

  http_send(pcb);
  return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
  (void)arg;
  (void)err;

  tcp_setprio(pcb, TCP_PRIO_NORMAL);
  tcp_recv(pcb, http_recv);
  return ERR_OK;
}

void http_server_init(void)
{
  http_pcb = tcp_new();
  if (http_pcb == NULL)
  {
    printf("HTTP server: tcp_new failed\r\n");
    return;
  }

  if (tcp_bind(http_pcb, IP_ADDR_ANY, 80) != ERR_OK)
  {
    printf("HTTP server: bind failed\r\n");
    return;
  }

  http_pcb = tcp_listen(http_pcb);
  if (http_pcb == NULL)
  {
    printf("HTTP server: listen failed\r\n");
    return;
  }

  tcp_accept(http_pcb, http_accept);
  printf("HTTP server: listening on port 80\r\n");
}
