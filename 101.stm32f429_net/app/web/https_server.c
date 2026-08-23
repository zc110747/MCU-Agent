/**
  ******************************************************************************
  * @file    https_server.c
  * @brief   HTTPS server: netconn (port 443) + mbedTLS in a FreeRTOS task.
  *
  *          Architecture:
  *            - "httpsd" listener task accepts connections and spawns one
  *              "httpsN" task per connection (up to HTTPS_MAX_CONNS). This
  *              makes browser parallel requests (6 connections by default)
  *              run concurrently instead of serially behind a single task,
  *              which previously stalled page loads / button actions.
  *            - Each connection keeps its TLS session alive and reuses it for
  *              multiple HTTP requests on the same TCP connection (HTTP/1.1
  *              keep-alive). The connection is only released after 60s of
  *              idle (no request), so the expensive ECDHE handshake is cached.
  *            - mbedTLS BIO callbacks are BLOCKING (call netconn_write /
  *              netconn_recv directly); a 1s recv timeout lets the request
  *              loop poll for the idle timeout without blocking forever.
  *            - All mbedTLS heap comes from a static pool in external SDRAM
  *              (app/mbedtls_pool.c, MBEDTLS_MEMORY_BUFFER_ALLOC_C).
  *            - RNG is a dev-grade xorshift64 (self-signed dev server only).
  ******************************************************************************
  */
#include "https_server.h"
#include "lwip/api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "web_page.h"
#include "web_serve.h"
#include "tls_creds.h"

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/version.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls_pool.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

#define HTTPS_PORT            443
#define HTTPS_STACK_SIZE      4096   /* mbedTLS ECDHE handshake needs room */
#define HTTPS_TASK_PRIORITY   3
#define HTTPS_REQ_BUF         2048   /* request line + headers + body */
#define HTTPS_MAX_CONNS       4      /* max concurrent TLS connections */
#define HTTPS_IDLE_MS         (60 * 1000)   /* idle timeout: release session */
#define HTTPS_POLL_MS         1000           /* recv poll period */

/* ---- shared config (parsed once at init) ---- */
static mbedtls_ssl_config g_conf;
static mbedtls_x509_crt g_srvcert;
static mbedtls_pk_context g_pkey;

/* ---- dev-grade software RNG (xorshift64) ---- */
static uint64_t g_rng_state;
static SemaphoreHandle_t s_rng_mutex = NULL;   /* protect g_rng_state */

/* ---- concurrency gate ---- */
static SemaphoreHandle_t s_conn_mutex = NULL;
static int               s_conn_count = 0;
/* Serialize TLS handshakes: mbedTLS is built WITHOUT MBEDTLS_THREADING_C, so
 * concurrent mbedtls_ssl_handshake() calls race on internal global state and
 * stall. Only one handshake runs at a time; once established, the per-connection
 * ssl contexts are independent and requests run concurrently. */
static SemaphoreHandle_t s_hs_gate = NULL;

static int https_rng(void *ctx, unsigned char *out, size_t len)
{
  (void)ctx;
  if (s_rng_mutex != NULL)
  {
    xSemaphoreTake(s_rng_mutex, portMAX_DELAY);
  }
  uint64_t s = g_rng_state;
  for (size_t i = 0; i < len; i++)
  {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    out[i] = (unsigned char)(s >> 32);
  }
  g_rng_state = s;
  if (s_rng_mutex != NULL)
  {
    xSemaphoreGive(s_rng_mutex);
  }
  return 0;
}

/* ---- per-connection context (no shared RX buffer across connections) ---- */
typedef struct
{
  struct netconn *conn;
  unsigned char   rxbuf[4096];   /* surplus from netbuf_copy, per connection */
  u16_t           rxlen;
  u16_t           rxpos;
} https_conn_ctx;

/* ---- BIO: direct netconn calls (blocking) ---- */
/* netconn_recv() returns whole netbufs, but mbedTLS asks for small chunks
 * (e.g. 5-byte TLS record headers).  Buffer the surplus so nothing is lost. */
static int https_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
  https_conn_ctx *c = (https_conn_ctx *)ctx;
  struct netconn *conn = c->conn;
  const unsigned char *p = buf;
  size_t remain = len;
  /* Same loop-and-retry pattern as http_send: a single netconn_write of a
   * large TLS record (e.g. 16 KB of an application-data record containing the
   * Vue JS bundle) can return ERR_MEM and silently truncate the response. */
  while (remain > 0)
  {
    size_t chunk = (remain > 1024) ? 1024 : remain;
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
      return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
  }
  return (int)len;
}

static int https_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
  https_conn_ctx *c = (https_conn_ctx *)ctx;
  struct netconn *conn = c->conn;

  /* serve from the buffered surplus first */
  if (c->rxpos < c->rxlen)
  {
    u16_t avail = c->rxlen - c->rxpos;
    size_t n = (len < avail) ? len : avail;
    memcpy(buf, &c->rxbuf[c->rxpos], n);
    c->rxpos += (u16_t)n;
    return (int)n;
  }
  c->rxlen = 0;
  c->rxpos = 0;

  /* buffer empty: pull the next netbuf entirely into the per-conn rxbuf */
  struct netbuf *nb = NULL;
  err_t err = netconn_recv(conn, &nb);
  if (err != ERR_OK)
  {
    /* With a recv timeout set on the conn, ERR_TIMEOUT means "no data yet":
     * surface it as WANT_READ so the request loop can poll the idle timer
     * instead of tearing the connection down. Any other error (peer closed,
     * aborted) is a real EOF. */
    if (err == ERR_TIMEOUT)
    {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return MBEDTLS_ERR_SSL_CONN_EOF;   /* peer closed the connection */
  }
  if (nb == NULL)
  {
    return MBEDTLS_ERR_SSL_WANT_READ;
  }
  u16_t n = netbuf_copy(nb, c->rxbuf, sizeof(c->rxbuf));
  netbuf_delete(nb);
  c->rxlen = n;

  size_t take = (len < c->rxlen) ? len : c->rxlen;
  memcpy(buf, c->rxbuf, take);
  c->rxpos = (u16_t)take;
  return (int)take;
}

/* ---- send callback for web_serve over TLS ---- */
static int https_send(void *ctx, const void *data, int len)
{
  mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)ctx;
  const unsigned char *p = (const unsigned char *)data;
  int remain = len;
  /* mbedtls_ssl_write can return short on a full TLS send buffer; loop until
   * the whole response has been queued into the BIO. */
  while (remain > 0)
  {
    int ret = mbedtls_ssl_write(ssl, p, (size_t)remain);
    if (ret > 0)
    {
      p += ret;
      remain -= ret;
    }
    else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
             ret == MBEDTLS_ERR_SSL_WANT_READ)
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

/* ---- per-connection task ---- */
static void https_conn_task(void *arg)
{
  https_conn_ctx *c = (https_conn_ctx *)arg;
  struct netconn *conn = c->conn;
  mbedtls_ssl_context ssl;
  int ret;

  mbedtls_ssl_init(&ssl);
  ret = mbedtls_ssl_setup(&ssl, &g_conf);
  if (ret != 0)
  {
    PRINT_LOG("HTTPS ssl_setup: -0x%04X\r\n", (unsigned)(-ret));
    goto cleanup;
  }
  mbedtls_ssl_set_bio(&ssl, c, https_bio_send, https_bio_recv, NULL);

  /* Serialize the handshake: mbedTLS here is NOT thread-safe, so only one
   * handshake may run at a time. Take the gate, handshake, then release so
   * the other (already accepted) connections can proceed. Application-data
   * processing below runs fully concurrently (per-connection ssl ctx). */
  if (s_hs_gate != NULL)
  {
    xSemaphoreTake(s_hs_gate, portMAX_DELAY);
  }
  /* Blocking handshake (TLS 1.2, ECDHE-ECDSA P-256) */
  ret = mbedtls_ssl_handshake(&ssl);
  if (s_hs_gate != NULL)
  {
    xSemaphoreGive(s_hs_gate);
  }
  if (ret != 0)
  {
    PRINT_LOG("HTTPS handshake: -0x%04X\r\n", (unsigned)(-ret));
    goto cleanup;
  }

  /* Arm a recv timeout so the request loop can poll the idle timer (the BIO
   * turns ERR_TIMEOUT into WANT_READ). Without this, netconn_recv blocks
   * forever and the 60s idle release never triggers. */
  netconn_set_recvtimeout(conn, pdMS_TO_TICKS(HTTPS_POLL_MS));

  /* Request loop with keep-alive: serve multiple HTTP requests on the same
   * TLS session. The connection is released only after HTTPS_IDLE_MS of
   * inactivity, caching the handshake for the browser's parallel requests. */
  TickType_t last_active = xTaskGetTickCount();
  for (;;)
  {
    /* Read one full request (headers end marker or buffer full). */
    unsigned char req[HTTPS_REQ_BUF];
    int reqlen = 0;
    int got_eof = 0;
    while (reqlen < (int)sizeof(req) - 1)
    {
      ret = mbedtls_ssl_read(&ssl, req + reqlen,
                             (size_t)((int)sizeof(req) - 1 - reqlen));
      if (ret > 0)
      {
        reqlen += ret;
        if (reqlen >= 4 && memmem(req, (size_t)reqlen, "\r\n\r\n", 4) != NULL)
        {
          break;
        }
      }
      else if (ret == MBEDTLS_ERR_SSL_WANT_READ)
      {
        /* peer sent nothing yet; check idle timeout */
        if ((xTaskGetTickCount() - last_active) > pdMS_TO_TICKS(HTTPS_IDLE_MS))
        {
          goto cleanup;
        }
        continue;
      }
      else
      {
        got_eof = 1;   /* peer closed or error */
        break;
      }
    }

    if (reqlen > 0)
    {
      req[reqlen] = '\0';
      last_active = xTaskGetTickCount();
      web_serve(conn, https_send, &ssl, (const char *)req, reqlen, 1);
      /* https_send -> mbedtls_ssl_write already pushes the record to the
       * wire; no explicit flush needed. */
    }

    if (got_eof)
    {
      break;
    }
    if (reqlen == 0)
    {
      /* idle timeout reached inside the inner read loop */
      if ((xTaskGetTickCount() - last_active) > pdMS_TO_TICKS(HTTPS_IDLE_MS))
      {
        break;
      }
    }
  }

cleanup:
  mbedtls_ssl_close_notify(&ssl);
  mbedtls_ssl_free(&ssl);

  /* Graceful close: let queued TX drain, then FIN, then delete after a
   * short delay so the peer does not see a RST. */
  netconn_close(conn);
  {
    struct netbuf *rb = NULL;
    TickType_t start = xTaskGetTickCount();
    while (netconn_recv(conn, &rb) == ERR_OK)
    {
      if (rb) netbuf_delete(rb);
      if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(300)) break;
    }
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  netconn_delete(conn);

  if (s_conn_mutex != NULL)
  {
    xSemaphoreTake(s_conn_mutex, portMAX_DELAY);
    if (s_conn_count > 0) s_conn_count--;
    xSemaphoreGive(s_conn_mutex);
  }

#if defined(MBEDTLS_MEMORY_DEBUG)
  size_t cur = 0, cur_blk = 0, max = 0, max_blk = 0;
  mbedtls_memory_buffer_alloc_cur_get(&cur, &cur_blk);
  mbedtls_memory_buffer_alloc_max_get(&max, &max_blk);
  PRINT_LOG("HTTPS conn done, TLS heap cur=%u max=%u\r\n",
            (unsigned)cur, (unsigned)max);
#endif

  vTaskDelete(NULL);   /* end this connection task */
}

/* ---- listener task ---- */
static void https_server_thread(void *arg)
{
  (void)arg;
  struct netconn *lc = NULL;
  struct netconn *newconn = NULL;
  err_t err;

  lc = netconn_new(NETCONN_TCP);
  if (lc == NULL)
  {
    PRINT_LOG("HTTPS: netconn_new failed\r\n");
    vTaskDelete(NULL);
    return;
  }
  if (netconn_bind(lc, IP_ADDR_ANY, HTTPS_PORT) != ERR_OK)
  {
    PRINT_LOG("HTTPS: bind 443 failed\r\n");
    netconn_delete(lc);
    vTaskDelete(NULL);
    return;
  }
  if (netconn_listen(lc) != ERR_OK)
  {
    PRINT_LOG("HTTPS: listen failed\r\n");
    netconn_delete(lc);
    vTaskDelete(NULL);
    return;
  }
  PRINT_LOG("HTTPS server: listening on port 443 (mbedTLS %s), max %d conns\r\n",
            MBEDTLS_VERSION_STRING, HTTPS_MAX_CONNS);

  for (;;)
  {
    err = netconn_accept(lc, &newconn);
    if (err != ERR_OK)
    {
      continue;
    }

    /* gate concurrent connections */
    int accept_it = 0;
    if (s_conn_mutex != NULL)
    {
      if (xSemaphoreTake(s_conn_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        if (s_conn_count < HTTPS_MAX_CONNS)
        {
          s_conn_count++;
          accept_it = 1;
        }
        xSemaphoreGive(s_conn_mutex);
      }
    }
    if (!accept_it)
    {
      /* too many connections: refuse this one gracefully */
      netconn_close(newconn);
      netconn_delete(newconn);
      continue;
    }

    https_conn_ctx *c = (https_conn_ctx *)
        pvPortMalloc(sizeof(https_conn_ctx));
    if (c == NULL)
    {
      netconn_close(newconn);
      netconn_delete(newconn);
      if (s_conn_mutex != NULL)
      {
        xSemaphoreTake(s_conn_mutex, portMAX_DELAY);
        if (s_conn_count > 0) s_conn_count--;
        xSemaphoreGive(s_conn_mutex);
      }
      continue;
    }
    memset(c, 0, sizeof(*c));
    c->conn = newconn;

    char name[12];
    static uint8_t tag = 0;
    snprintf(name, sizeof(name), "https%u", (unsigned)(tag++ % 100));
    if (xTaskCreate(https_conn_task, name, HTTPS_STACK_SIZE, c,
                    HTTPS_TASK_PRIORITY, NULL) != pdPASS)
    {
      vPortFree(c);
      netconn_close(newconn);
      netconn_delete(newconn);
      if (s_conn_mutex != NULL)
      {
        xSemaphoreTake(s_conn_mutex, portMAX_DELAY);
        if (s_conn_count > 0) s_conn_count--;
        xSemaphoreGive(s_conn_mutex);
      }
    }
    /* else: task owns newconn + c; it frees them on exit */
  }
}

/* ---- init ---- */
void https_server_init(void)
{
  mbedtls_memory_buffer_alloc_init(mbedtls_heap, MBEDTLS_POOL_SIZE);

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

  s_conn_mutex = xSemaphoreCreateMutex();
  s_rng_mutex  = xSemaphoreCreateMutex();
  s_hs_gate    = xSemaphoreCreateMutex();   /* binary: one handshake at a time */

  mbedtls_x509_crt_init(&g_srvcert);
  mbedtls_pk_init(&g_pkey);
  mbedtls_ssl_config_init(&g_conf);

  /* cert/key: mbedTLS wants the NUL terminator included in the length */
  int ret = mbedtls_x509_crt_parse(&g_srvcert,
                                   (const unsigned char *)tls_cert_pem,
                                   strlen(tls_cert_pem) + 1);
  if (ret != 0)
  {
    PRINT_LOG("HTTPS cert parse failed: -0x%04X\r\n", (unsigned)(-ret));
    return;
  }
  ret = mbedtls_pk_parse_key(&g_pkey,
                             (const unsigned char *)tls_key_pem,
                             strlen(tls_key_pem) + 1, NULL, 0, https_rng, NULL);
  if (ret != 0)
  {
    PRINT_LOG("HTTPS key parse failed: -0x%04X\r\n", (unsigned)(-ret));
    return;
  }

  ret = mbedtls_ssl_config_defaults(&g_conf, MBEDTLS_SSL_IS_SERVER,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0)
  {
    PRINT_LOG("HTTPS ssl_config_defaults failed: -0x%04X\r\n", (unsigned)(-ret));
    return;
  }
  mbedtls_ssl_conf_authmode(&g_conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&g_conf, https_rng, NULL);
  ret = mbedtls_ssl_conf_own_cert(&g_conf, &g_srvcert, &g_pkey);
  if (ret != 0)
  {
    PRINT_LOG("HTTPS own_cert failed: -0x%04X\r\n", (unsigned)(-ret));
    return;
  }

  if (xTaskCreate(https_server_thread, "httpsd", HTTPS_STACK_SIZE, NULL,
                  HTTPS_TASK_PRIORITY, NULL) != pdPASS)
  {
    PRINT_LOG("HTTPS: task create failed\r\n");
  }
}
