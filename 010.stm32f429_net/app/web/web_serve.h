/**
  ******************************************************************************
  * @file    web_serve.h
  * @brief   Shared web request handler: serves pages from the SD card
  *          (web/ dir) and the JSON /api endpoints over any transport.
  *
  *          The caller supplies a send callback so the same handler works
  *          for plain HTTP (netconn_write) and HTTPS (mbedtls_ssl_write).
  ******************************************************************************
  */
#ifndef __WEB_SERVE_H__
#define __WEB_SERVE_H__

#include "lwip/api.h"

/* Send callback: write len bytes of data to the connection.
 * Returns 0 on success. ctx is transport-specific. */
typedef int (*web_send_fn)(void *ctx, const void *data, int len);

/* Handle one HTTP request. req/reqlen is the raw request text (start line
 * + headers + optional body). send/sctx carry the response out. */
void web_serve(struct netconn *conn, web_send_fn send, void *sctx,
               const char *req, int reqlen);

/* I2C bus is shared by AP3216C/MPU9250 (and PCF8574); protect sensor reads
 * between the httpd and httpsd tasks. */
void web_i2c_lock(void);
void web_i2c_unlock(void);

/* One-time init: default netcfg + I2C mutex. Call before servers start. */
void web_serve_init(void);

#endif /* __WEB_SERVE_H__ */
