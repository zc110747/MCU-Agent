/**
  ******************************************************************************
  * @file    bsp_log.h
  * @brief   USART1 based console: printf retarget + lightweight log macros.
  ******************************************************************************
  */

#ifndef __BSP_LOG_H
#define __BSP_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/** Initialise USART1 (115200-8-N-1) and hook it up to printf(). */
GlobalType_t bsp_log_init(void);

/** Blocking write of a raw buffer to the console. */
void bsp_log_write(const char *data, int len);

#define LOG_I(fmt, ...)   printf("[I] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_W(fmt, ...)   printf("[W] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_E(fmt, ...)   printf("[E] " fmt "\r\n", ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LOG_H */
