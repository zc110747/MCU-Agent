/**
  ******************************************************************************
  * @file    web_serve.c
  * @brief   Shared web request handler (SD-card pages + JSON API).
  *
  *          Routing:
  *            GET /                    -> SD 0:/web/index.html (fallback flash page)
  *            GET /assets/xxx          -> SD 0:/web/assets/xxx
  *            GET /api/hardware        -> JSON hardware/sensor/IO state
  *            GET /api/network         -> JSON ip/mask/gw/mac
  *            POST /api/control        -> {led|beep:0|1}
  *            POST /api/network        -> {ip,mask,gw,mac} (persist + apply)
  *            POST /api/reset          -> reboot
  ******************************************************************************
  */
#include "web_serve.h"
#include "web_page.h"
#include "web_assets.h"
#include "hwinfo.h"          /* shared static/dynamic info (web/telnet/snmp) */
#include "netcfg.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "FreeRTOS.h"
#include "task.h"           /* xTaskGetSchedulerState */
#include "semphr.h"
#include <stdio.h>
#include <string.h>

#define REQ_BUF     2048         /* must hold request line + headers + body */
static SemaphoreHandle_t g_i2c_mutex = NULL;

extern struct netif gnetif;          /* lwip.h also declares it */
netcfg_t g_netcfg;                   /* default set by netcfg_init_defaults() */

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

/* one decimal place, no %f (newlib-nano) */
static void fmt1(char *out, float v)
{
  int neg = 0;
  if (v < 0.0f) { neg = 1; v = -v; }
  int i = (int)v;
  int f = (int)((v - (float)i) * 10.0f + 0.5f);
  if (f >= 10) { f = 0; i++; }
  /* out[] is 16 bytes: "-2147483648.0" fits */
  if (neg)
    sprintf(out, "-%d.%d", i, f);
  else
    sprintf(out, "%d.%d", i, f);
}

static void send_json(web_send_fn send, void *sctx, const char *json, int len)
{
  char hdr[96];
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                   "Content-Length: %d\r\nConnection: close\r\n\r\n", len);
  send(sctx, hdr, n);
  send(sctx, json, len);
}

/* ------------------------------------------------------------------ */
/* API handlers                                                       */
/* ------------------------------------------------------------------ */

static void api_hardware(web_send_fn send, void *sctx)
{
  hwinfo_static_t  sta;
  hwinfo_dynamic_t dyn;
  char f[9][16];      /* one buffer per value (no %f in newlib-nano) */
  char j[1024];
  int n;

  /* Snapshot the shared info (atomic whole-struct copy under critical sec). */
  hwinfo_static_copy(&sta);
  hwinfo_dynamic_copy(&dyn);

  fmt1(f[0], dyn.ax); fmt1(f[1], dyn.ay); fmt1(f[2], dyn.az);
  fmt1(f[3], dyn.gx); fmt1(f[4], dyn.gy); fmt1(f[5], dyn.gz);
  fmt1(f[6], dyn.mx); fmt1(f[7], dyn.my); fmt1(f[8], dyn.mz);

  n = snprintf(j, sizeof(j),
    "{\"mcu\":\"%s\",\"clock\":\"%s\",\"tasks\":%u,"
    "\"ap3216c\":{\"lux\":%u,\"ps\":%u,\"ir\":%u,\"valid\":%u},"
    "\"mpu9250\":{\"ax\":%s,\"ay\":%s,\"az\":%s,"
    "\"gx\":%s,\"gy\":%s,\"gz\":%s,"
    "\"mx\":%s,\"my\":%s,\"mz\":%s},"
    "\"led\":%u,\"beep\":%u}",
    sta.mcu, sta.clock, (unsigned)sta.freertos_tasks,
    (unsigned)dyn.lux, (unsigned)dyn.ps, (unsigned)dyn.ir,
    (unsigned)dyn.sensor_valid,
    f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8],
    (unsigned)dyn.led_on, (unsigned)dyn.beep_on);

  send_json(send, sctx, j, n);
}

static void api_network_get(web_send_fn send, void *sctx)
{
  char j[256];
  int n = snprintf(j, sizeof(j),
    "{\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\",\"mac\":\"%s\"}",
    g_netcfg.ip, g_netcfg.mask, g_netcfg.gw, g_netcfg.mac);
  send_json(send, sctx, j, n);
}

/* extract "key":value; the key must be a complete JSON token (preceded by
 * '"'/'{'/',' and followed by '"'), so "beep" does not match inside another
 * key. Returns 1 if found. */
static int json_get_int(const char *body, const char *key, int *val)
{
  size_t klen = strlen(key);
  const char *p = body;
  while ((p = strstr(p, key)) != NULL)
  {
    char prev = (p > body) ? p[-1] : '{';
    const char *after = p + klen;
    if ((prev == '"' || prev == '{' || prev == ',') && *after == '"')
    {
      const char *colon = strchr(after, ':');
      if (colon != NULL)
      {
        const char *num = colon + 1;
        while (*num == ' ' || *num == '\t') num++;
        *val = atoi(num);
        return 1;
      }
    }
    p = after;
  }
  return 0;
}

static int json_get_str(const char *body, const char *key, char *out, int maxlen)
{
  const char *p = strstr(body, key);
  const char *s, *e;
  int n;
  if (p == NULL) return 0;
  s = strchr(p, '"');
  if (s == NULL) return 0;
  s = strchr(s + 1, '"');
  if (s == NULL) return 0;
  s += 1;
  e = strchr(s, '"');
  if (e == NULL) return 0;
  n = (int)(e - s);
  if (n >= maxlen) n = maxlen - 1;
  memcpy(out, s, (size_t)n);
  out[n] = '\0';
  return 1;
}

static void api_control(const char *body, web_send_fn send, void *sctx)
{
  int v;
  if (json_get_int(body, "led", &v))
  {
    /* Routes to the shared control path (PB0, non-heartbeat LED). */
    hwinfo_set_led((uint8_t)(v ? 1 : 0));
  }
  if (json_get_int(body, "beep", &v))
  {
    hwinfo_set_beep((uint8_t)(v ? 1 : 0));
  }
  send_json(send, sctx, "{\"ok\":true}", 11);
}

static void api_network_set(const char *body, web_send_fn send, void *sctx)
{
  char tmp[20];

  if (json_get_str(body, "ip", tmp, sizeof(tmp)))
  {
    strncpy(g_netcfg.ip, tmp, sizeof(g_netcfg.ip) - 1);
    g_netcfg.ip[sizeof(g_netcfg.ip) - 1] = '\0';
  }
  if (json_get_str(body, "mask", tmp, sizeof(tmp)))
  {
    strncpy(g_netcfg.mask, tmp, sizeof(g_netcfg.mask) - 1);
    g_netcfg.mask[sizeof(g_netcfg.mask) - 1] = '\0';
  }
  if (json_get_str(body, "gw", tmp, sizeof(tmp)))
  {
    strncpy(g_netcfg.gw, tmp, sizeof(g_netcfg.gw) - 1);
    g_netcfg.gw[sizeof(g_netcfg.gw) - 1] = '\0';
  }
  if (json_get_str(body, "mac", tmp, sizeof(tmp)))
  {
    strncpy(g_netcfg.mac, tmp, sizeof(g_netcfg.mac) - 1);
    g_netcfg.mac[sizeof(g_netcfg.mac) - 1] = '\0';
  }

  /* persist to EEPROM. Changes do NOT take effect until the next reset
   * (netif is configured once at boot from g_netcfg, and MAC via netconn). */
  netcfg_save(&g_netcfg);

  send_json(send, sctx, "{\"ok\":true,\"apply\":\"reboot\"}", 27);
}

static void api_reset(web_send_fn send, void *sctx)
{
  send_json(send, sctx, "{\"ok\":true}", 11);
  vTaskDelay(pdMS_TO_TICKS(100));      /* let the response flush */
  NVIC_SystemReset();
}

/* ------------------------------------------------------------------ */
/* static file serving (built-in assets)                              */
/* ------------------------------------------------------------------ */

/* Serve a built-in asset by URL path. Returns 1 if sent, 0 if not found. */
static int send_asset(web_send_fn send, void *sctx, const char *url)
{
  char hdr[128];
  int n;

  for (int i = 0; i < web_assets_count; i++)
  {
    const web_asset_t *a = &web_assets[i];
    if (strcmp(a->path, url) != 0)
    {
      continue;
    }
    n = snprintf(hdr, sizeof(hdr),
                 "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
                 "Content-Length: %u\r\nConnection: close\r\n\r\n",
                 a->ctype, (unsigned)a->len);
    send(sctx, hdr, n);
    send(sctx, (const char *)a->data, (int)a->len);
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* main dispatch                                                      */
/* ------------------------------------------------------------------ */

void web_serve(struct netconn *conn, web_send_fn send, void *sctx,
               const char *req, int reqlen)
{
  char method[8], path[192], body[512];
  const char *p, *e;
  int mlen, plen, blen = 0;

  (void)conn;

  /* parse request line: METHOD SP PATH SP HTTP/x.x */
  e = memchr(req, '\r', (size_t)reqlen);
  if (e == NULL) { e = req + reqlen; }
  p = req;
  while (p < e && *p == ' ') p++;
  mlen = (int)(e - p);
  if (mlen > (int)sizeof(method) - 1) mlen = (int)sizeof(method) - 1;
  memcpy(method, p, (size_t)mlen); method[mlen] = '\0';

  p = memchr(req, ' ', (size_t)(e - req));
  if (p == NULL) p = req + (e - req);
  while (p < e && *p == ' ') p++;
  /* path ends at the next space (before HTTP/x.x) or at the line end */
  {
    const char *pe = p;
    while (pe < e && *pe != ' ') pe++;
    plen = (int)(pe - p);
  }
  if (plen > (int)sizeof(path) - 1) plen = (int)sizeof(path) - 1;
  memcpy(path, p, (size_t)plen); path[plen] = '\0';

  /* body after \r\n\r\n */
  {
    const char *b = memchr(req, '\r', (size_t)reqlen);
    const char *hdr_end = req;
    int i;
    for (i = 0; i < reqlen - 3; i++)
    {
      if (req[i] == '\r' && req[i + 1] == '\n' && req[i + 2] == '\r' && req[i + 3] == '\n')
      {
        hdr_end = req + i + 4;
        break;
      }
    }
    (void)b;
    blen = reqlen - (int)(hdr_end - req);
    if (blen > (int)sizeof(body) - 1) blen = (int)sizeof(body) - 1;
    memcpy(body, hdr_end, (size_t)blen);
    body[blen] = '\0';
  }

  /* ---- API routes ---- */
  if (strncmp(path, "/api/", 5) == 0)
  {
    if (strcmp(path, "/api/hardware") == 0)
    {
      api_hardware(send, sctx);
    }
    else if (strcmp(path, "/api/network") == 0)
    {
      if (strncmp(method, "POST", 4) == 0)
        api_network_set(body, send, sctx);
      else
        api_network_get(send, sctx);
    }
    else if (strcmp(path, "/api/control") == 0)
    {
      api_control(body, send, sctx);
    }
    else if (strcmp(path, "/api/reset") == 0)
    {
      api_reset(send, sctx);
    }
    else
    {
      send_json(send, sctx, "{\"error\":\"unknown\"}", 18);
    }
    return;
  }

  /* ---- page routes (built-in assets) ---- */
  {
    const char *url = path;
    if (strcmp(path, "/") == 0)
    {
      url = "/index.html";
    }

    if (send_asset(send, sctx, url))
    {
      return;
    }

    /* fallback: built-in flash page */
    {
      char hdr[128];
      int n = snprintf(hdr, sizeof(hdr),
                       "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                       "Content-Length: %u\r\nConnection: close\r\n\r\n",
                       (unsigned)strlen(HTTP_PAGE));
      send(sctx, hdr, n);
      send(sctx, HTTP_PAGE, (int)strlen(HTTP_PAGE));
    }
  }
}

/* ------------------------------------------------------------------ */
/* shared I2C mutex                                                   */
/* ------------------------------------------------------------------ */

void web_i2c_lock(void)
{
  /* Before the scheduler starts (e.g. netcfg_load during boot) the system is
   * single-threaded: there is no bus contention, and creating/taking a mutex
   * here would leave BASEPRI set. Skip locking until the scheduler is up. */
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    return;

  if (g_i2c_mutex == NULL)
  {
    g_i2c_mutex = xSemaphoreCreateMutex();
  }
  if (g_i2c_mutex != NULL)
  {
    xSemaphoreTake(g_i2c_mutex, portMAX_DELAY);
  }
}

void web_i2c_unlock(void)
{
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    return;

  if (g_i2c_mutex != NULL)
  {
    xSemaphoreGive(g_i2c_mutex);
  }
}

/* called once from main before the servers start.
 * Note: the I2C mutex is created lazily in web_i2c_lock() (first request),
 * because xSemaphoreCreate* before vTaskStartScheduler leaves BASEPRI set. */
void web_serve_init(void)
{
  /* defaults first, then load persisted block from EEPROM (if valid). */
  netcfg_init_defaults(&g_netcfg);
  netcfg_load(&g_netcfg);
}
