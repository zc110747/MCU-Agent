/**
  ******************************************************************************
  * @file    netcfg.h
  * @brief   Network parameter store with AT24C02 EEPROM persistence.
  *
  *          Parameters are persisted in a fixed layout at EEPROM address 0:
  *            [ head(1B)=0xAA ][ ip ][ mask ][ gw ][ mac ][ crc16(2B) ]
  *          On boot netcfg_load() reads the block, verifies head + CRC16 over
  *          the whole data region. On any failure the compiled-in defaults
  *          are used. Web/shell changes are written back via netcfg_save() but
  *          intentionally do NOT take effect until the next reset (the netif is
  *          configured once at boot from g_netcfg).
  ******************************************************************************
  */
#ifndef __NETCFG_H__
#define __NETCFG_H__

#include <stdint.h>

#define NETCFG_IP_LEN   16
#define NETCFG_MAC_LEN  18

/* ---- persisted block layout (must match netcfg.c eeprom pack/unpack) ---- */
#define NETCFG_HEAD       0xAAU       /* magic marking a valid block */
#define NETCFG_HEAD_SIZE  1U
#define NETCFG_CRC_SIZE   2U
/* data region = ip+mask+gw+mac (no head/crc) */
#define NETCFG_DATA_SIZE  (NETCFG_IP_LEN + NETCFG_IP_LEN + NETCFG_IP_LEN + \
                           NETCFG_MAC_LEN)

typedef struct {
  char  ip[NETCFG_IP_LEN];    /* e.g. "192.168.10.99"  */
  char  mask[NETCFG_IP_LEN];  /* e.g. "255.255.255.0" */
  char  gw[NETCFG_IP_LEN];    /* e.g. "192.168.10.1"  */
  char  mac[NETCFG_MAC_LEN];  /* e.g. "00:80:E1:42:10:99" */
} netcfg_t;

/* Runtime network config (loaded from EEPROM or defaults). */
extern netcfg_t g_netcfg;

/* Fill cfg with the compiled-in defaults. */
void netcfg_init_defaults(netcfg_t *cfg);

/* Load persisted params from EEPROM into cfg. Returns 1 on valid block read
 * (and updates cfg), 0 if EEPROM blank/corrupt (cfg left at defaults). */
int netcfg_load(netcfg_t *cfg);

/* Save params to EEPROM (head + data + CRC16). Returns 1 on success. */
int netcfg_save(const netcfg_t *cfg);

/* CRC16-CCITT (0x1021 poly, init 0xFFFF) over buf[0..len). Used both for the
 * EEPROM integrity check and exposed so a host tool can verify in the same
 * way. */
uint16_t netcfg_crc16(const uint8_t *buf, uint16_t len);

#endif /* __NETCFG_H__ */
