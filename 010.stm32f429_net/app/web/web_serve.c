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
#include "bsp_ap3216.h"
#include "bsp_mpu9250.h"
#include "bsp_led.h"
#include "bsp_pcf8574.h"
#include "netcfg.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

#define REQ_BUF     2048         /* must hold request line + headers + body */

/* ---- shared IO state ---- */
static uint8_t g_led_on  = 0;
static uint8_t g_beep_on = 0;
static SemaphoreHandle_t g_i2c_mutex = NULL;

extern struct netif gnetif;          /* lwip.h also declares it */
netcfg_t g_netcfg;                   /* default set by netcfg_init_defaults() */

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static void netcfg_init_defaults(void)
{
  strcpy(g_netcfg.ip,   "192.168.10.99");
  strcpy(g_netcfg.mask, "255.255.255.0");
  strcpy(g_netcfg.gw,   "192.168.10.1");
  strcpy(g_netcfg.mac,  "00:80:E1:00:00:00");
}

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
  ap3216c_data_t als;
  mpu9250_data_t imu;
  char f[9][16];      /* one buffer per value (no %f in newlib-nano) */
  char j[1024];
  int n;

  web_i2c_lock();
  int ok1 = bsp_ap3216c_read(&als);
  int ok2 = bsp_mpu9250_read(&imu);
  web_i2c_unlock();

  if (ok1 != 0) { als.lux = als.ps = als.ir = 0; }
  if (ok2 != 0) { imu.ax = imu.ay = imu.az = 0;
                  imu.gx = imu.gy = imu.gz = 0;
                  imu.mx = imu.my = imu.mz = 0; }

  fmt1(f[0], imu.ax); fmt1(f[1], imu.ay); fmt1(f[2], imu.az);
  fmt1(f[3], imu.gx); fmt1(f[4], imu.gy); fmt1(f[5], imu.gz);
  fmt1(f[6], imu.mx); fmt1(f[7], imu.my); fmt1(f[8], imu.mz);

  n = snprintf(j, sizeof(j),
    "{\"mcu\":\"STM32F429IGT6\",\"clock\":\"180 MHz\","
    "\"ap3216c\":{\"lux\":%u,\"ps\":%u,\"ir\":%u},"
    "\"mpu9250\":{\"ax\":%s,\"ay\":%s,\"az\":%s,"
    "\"gx\":%s,\"gy\":%s,\"gz\":%s,"
    "\"mx\":%s,\"my\":%s,\"mz\":%s},"
    "\"led\":%u,\"beep\":%u}",
    (unsigned)als.lux, (unsigned)als.ps, (unsigned)als.ir,
    f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8],
    (unsigned)g_led_on, (unsigned)g_beep_on);

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

/* extract "key":value after the given key; returns 1 if found */
static int json_get_int(const char *body, const char *key, int *val)
{
  const char *p = strstr(body, key);
  if (p == NULL) return 0;
  p = strchr(p, ':');
  if (p == NULL) return 0;
  *val = atoi(p + 1);
  return 1;
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
    g_led_on = (uint8_t)(v ? 1 : 0);
    if (g_led_on) BSP_LED_On(0); else BSP_LED_Off(0);
  }
  if (json_get_int(body, "beep", &v))
  {
    g_beep_on = (uint8_t)(v ? 1 : 0);
    if (g_beep_on) BSP_BEEP_On(); else BSP_BEEP_Off();
  }
  send_json(send, sctx, "{\"ok\":true}", 11);
}

static void api_network_set(const char *body, web_send_fn send, void *sctx)
{
  char tmp[20];
  ip4_addr_t ip, mask, gw;

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

  /* persist to SD so it survives a reboot */
  netcfg_save(&g_netcfg);

  /* apply ip/mask/gw immediately (MAC takes effect after reboot) */
  if (ipaddr_aton(g_netcfg.ip, &ip) && ipaddr_aton(g_netcfg.mask, &mask) &&
      ipaddr_aton(g_netcfg.gw, &gw))
  {
    netif_set_addr(&gnetif, &ip, &mask, &gw);
  }

  send_json(send, sctx, "{\"ok\":true}", 11);
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
  netcfg_init_defaults();
}
