/**
  ******************************************************************************
  * @file    netcfg.h
  * @brief   Network parameter store (in-RAM only, no SD-card persistence).
  ******************************************************************************
  */
#ifndef __NETCFG_H__
#define __NETCFG_H__

#include <stdint.h>

#define NETCFG_IP_LEN   16
#define NETCFG_MAC_LEN  18

typedef struct {
  char ip[NETCFG_IP_LEN];    /* e.g. "192.168.10.99"  */
  char mask[NETCFG_IP_LEN];  /* e.g. "255.255.255.0" */
  char gw[NETCFG_IP_LEN];    /* e.g. "192.168.10.1"  */
  char mac[NETCFG_MAC_LEN];  /* e.g. "00:80:E1:42:10:99" */
} netcfg_t;

/* Runtime network config (compiled-in defaults; no SD persistence). */
extern netcfg_t g_netcfg;

/* Load persisted params. Returns 0 (no persistent store); cfg left unchanged. */
int netcfg_load(netcfg_t *cfg);

/* Save params. Returns 1 (accepted) but not persisted across reboot. */
int netcfg_save(const netcfg_t *cfg);

#endif /* __NETCFG_H__ */
