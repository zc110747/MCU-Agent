/**
  ******************************************************************************
  * @file    https_server.c
  * @brief   HTTPS server: netconn (port 443) + mbedTLS in a FreeRTOS task.
  *
  *          Architecture (simplest for a multitasking build):
  *            - Single "httpsd" task accepts one connection at a time.
  *            - mbedTLS runs BLOCKING: the BIO callbacks call netconn_write /
  *              netconn_recv directly, so the handshake/IO block the task
  *              (letting the scheduler run everything else) and there is no
  *              WANT_READ/WANT_WRITE state machine to drive.
  *            - All mbedTLS heap comes from a static pool in external SDRAM
  *              (app/mbedtls_pool.c, MBEDTLS_MEMORY_BUFFER_ALLOC_C).
  *            - RNG is a dev-grade xorshift64 (self-signed dev server only).
  ******************************************************************************
  */
#include "https_server.h"
#include "lwip/api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "web_page.h"
#include "tls_creds.h"

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/version.h"
#include "mbedtls/memory_buffer_alloc.h"

#include <stdio.h>
#include <string.h>

#define HTTPS_PORT            443
#define HTTPS_STACK_SIZE      2048   /* mbedTLS ECDHE handshake needs room */
#define HTTPS_TASK_PRIORITY   3
#define HTTPS_REQ_BUF         512

/* ---- shared config (parsed once at init) ---- */
static mbedtls_ssl_config g_conf;
static mbedtls_x509_crt g_srvcert;
static mbedtls_pk_context g_pkey;

/* ---- dev-grade software RNG (xorshift64) ---- */
static uint64_t g_rng_state;

static int https_rng(void *ctx, unsigned char *out, size_t len)
{
  (void)ctx;
  uint64_t s = g_rng_state;
  for (size_t i = 0; i < len; i++)
  {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    out[i] = (unsigned char)(s >> 32);
  }
  g_rng_state = s;
  return 0;
}

/* ---- BIO: direct netconn calls (blocking) ---- */
/* netconn_recv() returns whole netbufs, but mbedTLS asks for small chunks
 * (e.g. 5-byte TLS record headers).  Buffer the surplus so nothing is lost. */
static unsigned char g_rxbuf[4096];
static u16_t g_rxlen = 0;
static u16_t g_rxpos = 0;

static int https_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
  struct netconn *conn = (struct netconn *)ctx;
  err_t err = netconn_write(conn, buf, (u16_t)len, NETCONN_COPY);
  return (err == ERR_OK) ? (int)len : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int https_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
  struct netconn *conn = (struct netconn *)ctx;

  /* serve from the buffered surplus first */
  if (g_rxpos < g_rxlen)
  {
    u16_t avail = g_rxlen - g_rxpos;
    size_t n = (len < avail) ? len : avail;
    memcpy(buf, &g_rxbuf[g_rxpos], n);
    g_rxpos += (u16_t)n;
    return (int)n;
  }
  g_rxlen = 0;
  g_rxpos = 0;

  /* buffer empty: pull the next netbuf entirely into g_rxbuf */
  struct netbuf *nb = NULL;
  err_t err = netconn_recv(conn, &nb);
  if (err != ERR_OK)
  {
    return MBEDTLS_ERR_SSL_CONN_EOF;   /* peer closed the connection */
  }
  if (nb == NULL)
  {
    return MBEDTLS_ERR_SSL_WANT_READ;
  }
  u16_t n = netbuf_copy(nb, g_rxbuf, sizeof(g_rxbuf));
  netbuf_delete(nb);
  g_rxlen = n;

  size_t take = (len < g_rxlen) ? len : g_rxlen;
  memcpy(buf, g_rxbuf, take);
  g_rxpos = (u16_t)take;
  return (int)take;
}

/* ---- per-connection TLS session ---- */
static void https_serve_conn(struct netconn *newconn)
{
  mbedtls_ssl_context ssl;
  int ret;

  mbedtls_ssl_init(&ssl);
  ret = mbedtls_ssl_setup(&ssl, &g_conf);
  if (ret != 0)
  {
    printf("HTTPS ssl_setup: -0x%04X\r\n", (unsigned)(-ret));
    mbedtls_ssl_free(&ssl);
    return;
  }
  mbedtls_ssl_set_bio(&ssl, newconn, https_bio_send, https_bio_recv, NULL);

  /* Blocking handshake (TLS 1.2, ECDHE-ECDSA P-256) */
  ret = mbedtls_ssl_handshake(&ssl);
  if (ret != 0)
  {
    printf("HTTPS handshake: -0x%04X\r\n", (unsigned)(-ret));
    mbedtls_ssl_free(&ssl);
    return;
  }

  /* Drain the request (keeps close() clean, like the HTTP server) */
  unsigned char req[HTTPS_REQ_BUF];
  ret = mbedtls_ssl_read(&ssl, req, sizeof(req));
  (void)ret;

  /* Build + send the response over TLS */
  int page_len = (int)strlen(HTTP_PAGE);
  char resp[2048];
  int resp_len = snprintf(resp, sizeof(resp),
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Content-Length: %d\r\n"
      "Connection: close\r\n"
      "\r\n%s", page_len, HTTP_PAGE);

  mbedtls_ssl_write(&ssl, (const unsigned char *)resp, (size_t)resp_len);
  mbedtls_ssl_close_notify(&ssl);
  mbedtls_ssl_free(&ssl);

#if defined(MBEDTLS_MEMORY_DEBUG)
  size_t cur = 0, cur_blk = 0, max = 0, max_blk = 0;
  mbedtls_memory_buffer_alloc_cur_get(&cur, &cur_blk);
  mbedtls_memory_buffer_alloc_max_get(&max, &max_blk);
  printf("HTTPS conn done, TLS heap cur=%u max=%u\r\n",
         (unsigned)cur, (unsigned)max);
#endif
}

/* ---- task ---- */
static void https_server_thread(void *arg)
{
  (void)arg;
  struct netconn *conn = NULL;
  struct netconn *newconn = NULL;
  err_t err;

  conn = netconn_new(NETCONN_TCP);
  if (conn == NULL)
  {
    printf("HTTPS: netconn_new failed\r\n");
    vTaskDelete(NULL);
    return;
  }
  if (netconn_bind(conn, IP_ADDR_ANY, HTTPS_PORT) != ERR_OK)
  {
    printf("HTTPS: bind 443 failed\r\n");
    netconn_delete(conn);
    vTaskDelete(NULL);
    return;
  }
  if (netconn_listen(conn) != ERR_OK)
  {
    printf("HTTPS: listen failed\r\n");
    netconn_delete(conn);
    vTaskDelete(NULL);
    return;
  }
  printf("HTTPS server: listening on port 443 (mbedTLS %s)\r\n",
         MBEDTLS_VERSION_STRING);

  for (;;)
  {
    err = netconn_accept(conn, &newconn);
    if (err != ERR_OK)
    {
      continue;
    }
    https_serve_conn(newconn);
    netconn_close(newconn);
    netconn_delete(newconn);
  }
}

/* ---- init ---- */
void https_server_init(void)
{
  extern uint8_t mbedtls_heap[];
  mbedtls_memory_buffer_alloc_init(mbedtls_heap, 32u * 1024u);

  /* seed the software RNG */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  g_rng_state = (uint64_t)DWT->CYCCNT
              ^ (uint64_t)HAL_GetTick() * 2654435761u
              ^ ((uint64_t)(uintptr_t)&g_rng_state)
              ^ 0x9E3779B97F4A7C15ULL;
  if (g_rng_state == 0)
  {
    g_rng_state = 0x853C49E6748FEA9BULL;
  }

  mbedtls_x509_crt_init(&g_srvcert);
  mbedtls_pk_init(&g_pkey);
  mbedtls_ssl_config_init(&g_conf);

  /* cert/key: mbedTLS wants the NUL terminator included in the length */
  int ret = mbedtls_x509_crt_parse(&g_srvcert,
                                   (const unsigned char *)tls_cert_pem,
                                   strlen(tls_cert_pem) + 1);
  if (ret != 0)
  {
    printf("HTTPS cert parse failed: -0x%04X\r\n", (unsigned)(-ret));
    return;
  }
  ret = mbedtls_pk_parse_key(&g_pkey,
                             (const unsigned char *)tls_key_pem,
                             strlen(tls_key_pem) + 1, NULL, 0, https_rng, NULL);
  if (ret != 0)
  {
    printf("HTTPS key parse failed: -0x%04X\r\n", (unsigned)(-ret));
    return;
  }

  ret = mbedtls_ssl_config_defaults(&g_conf, MBEDTLS_SSL_IS_SERVER,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0)
  {
    printf("HTTPS ssl_config_defaults failed: -0x%04X\r\n", (unsigned)(-ret));
    return;
  }
  mbedtls_ssl_conf_authmode(&g_conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&g_conf, https_rng, NULL);
  ret = mbedtls_ssl_conf_own_cert(&g_conf, &g_srvcert, &g_pkey);
  if (ret != 0)
  {
    printf("HTTPS own_cert failed: -0x%04X\r\n", (unsigned)(-ret));
    return;
  }

  if (xTaskCreate(https_server_thread, "httpsd", HTTPS_STACK_SIZE, NULL,
                  HTTPS_TASK_PRIORITY, NULL) != pdPASS)
  {
    printf("HTTPS: task create failed\r\n");
  }
}
