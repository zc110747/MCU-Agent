/**
  ******************************************************************************
  * @file    netcfg.c
  * @brief   Network parameter store with AT24C02 EEPROM persistence.
  *
  *          Lifecycle:
  *            - netcfg_init_defaults(): set compiled-in defaults.
  *            - netcfg_load(): read the packed block from EEPROM at addr 0,
  *              verify head + CRC16 over the data region; on success copy into
  *              cfg, on failure leave cfg at defaults.
  *            - netcfg_save(): pack head + data + CRC16 and write to EEPROM.
  *
  *          Threading: netcfg_load runs before the scheduler (single thread).
  *          netcfg_save runs from shell/web tasks; the I2C bus is shared with
  *          the sensors and the EEPROM driver takes the shared bus mutex
  *          internally, so callers need not lock.
  ******************************************************************************
  */
#include "netcfg.h"
#include "bsp_eeprom_24c02.h"

#include <string.h>

/* g_netcfg is defined in web_serve.c; this module is its persistence layer.
 * (Kept in web_serve.c so the single definition lives with web_serve_init.) */
extern netcfg_t g_netcfg;

/* ------------------------------------------------------------------ */
/* defaults                                                           */
/* ------------------------------------------------------------------ */

void netcfg_init_defaults(netcfg_t *cfg)
{
  strcpy(cfg->ip,   "192.168.10.99");
  strcpy(cfg->mask, "255.255.255.0");
  strcpy(cfg->gw,   "192.168.10.1");
  strcpy(cfg->mac,  "00:80:E1:00:00:00");
}

/* ------------------------------------------------------------------ */
/* CRC16-CCITT (poly 0x1021, init 0xFFFF)                             */
/* ------------------------------------------------------------------ */

uint16_t netcfg_crc16(const uint8_t *buf, uint16_t len)
{
  uint16_t crc = 0xFFFFU;
  for (uint16_t i = 0; i < len; i++)
  {
    crc ^= (uint16_t)buf[i] << 8;
    for (int b = 0; b < 8; b++)
    {
      if (crc & 0x8000U) crc = (uint16_t)((crc << 1) ^ 0x1021U);
      else               crc = (uint16_t)(crc << 1);
    }
  }
  return crc;
}

/* ------------------------------------------------------------------ */
/* pack / unpack between netcfg_t and the EEPROM byte block           */
/* ------------------------------------------------------------------ */

/* layout at eeprom addr 0:
 *   [0]            head (0xAA)
 *   [1 ..15]       ip   (15 bytes + NUL)
 *   [16..30]       mask (15 bytes + NUL)
 *   [31..45]       gw   (15 bytes + NUL)
 *   [46..63]       mac  (17 bytes + NUL)
 *   [64..65]       crc16 over [0..63]
 * total = 69 bytes (well within 256-byte 24C02).
 */
#define BLK_OFF_HEAD   0U
#define BLK_OFF_IP     (BLK_OFF_HEAD + NETCFG_HEAD_SIZE)                 /* 1 */
#define BLK_OFF_MASK   (BLK_OFF_IP + NETCFG_IP_LEN)                      /* 17 */
#define BLK_OFF_GW     (BLK_OFF_MASK + NETCFG_IP_LEN)                    /* 33 */
#define BLK_OFF_MAC    (BLK_OFF_GW + NETCFG_IP_LEN)                      /* 49 */
#define BLK_OFF_CRC    (BLK_OFF_MAC + NETCFG_MAC_LEN)                    /* 67 */
#define BLK_TOTAL      (BLK_OFF_CRC + NETCFG_CRC_SIZE)                   /* 69 */

static void pack_block(const netcfg_t *cfg, uint8_t *blk)
{
  memset(blk, 0, BLK_TOTAL);
  blk[BLK_OFF_HEAD] = (uint8_t)NETCFG_HEAD;
  memcpy(blk + BLK_OFF_IP,   cfg->ip,   NETCFG_IP_LEN);
  memcpy(blk + BLK_OFF_MASK, cfg->mask, NETCFG_IP_LEN);
  memcpy(blk + BLK_OFF_GW,   cfg->gw,   NETCFG_IP_LEN);
  memcpy(blk + BLK_OFF_MAC,  cfg->mac,  NETCFG_MAC_LEN);
  uint16_t crc = netcfg_crc16(blk, BLK_OFF_CRC);
  blk[BLK_OFF_CRC + 0] = (uint8_t)(crc & 0xFFU);
  blk[BLK_OFF_CRC + 1] = (uint8_t)((crc >> 8) & 0xFFU);
}

static void unpack_block(netcfg_t *cfg, const uint8_t *blk)
{
  memcpy(cfg->ip,   blk + BLK_OFF_IP,   NETCFG_IP_LEN);
  memcpy(cfg->mask, blk + BLK_OFF_MASK, NETCFG_IP_LEN);
  memcpy(cfg->gw,   blk + BLK_OFF_GW,   NETCFG_IP_LEN);
  memcpy(cfg->mac,  blk + BLK_OFF_MAC,  NETCFG_MAC_LEN);
}

/* ------------------------------------------------------------------ */
/* load / save                                                        */
/* ------------------------------------------------------------------ */

int netcfg_load(netcfg_t *cfg)
{
  uint8_t blk[BLK_TOTAL];

  /* Read the whole block from EEPROM address 0. */
  if (EEPROM24_Read(0, blk, BLK_TOTAL) != 0)
  {
    /* Bus/device error: keep defaults. */
    return 0;
  }

  /* head magic present? */
  if (blk[BLK_OFF_HEAD] != (uint8_t)NETCFG_HEAD)
  {
    return 0;
  }

  /* CRC16 over [head .. mac] must match the stored value. */
  uint16_t crc = netcfg_crc16(blk, BLK_OFF_CRC);
  uint16_t stored = (uint16_t)((blk[BLK_OFF_CRC + 1] << 8) |
                               (blk[BLK_OFF_CRC + 0]));
  if (crc != stored)
  {
    return 0;
  }

  unpack_block(cfg, blk);
  return 1;
}

int netcfg_save(const netcfg_t *cfg)
{
  uint8_t blk[BLK_TOTAL];
  pack_block(cfg, blk);

  /* EEPROM24_Write takes the shared I2C bus mutex internally. */
  int rc = EEPROM24_Write(0, blk, BLK_TOTAL);

  return (rc == 0) ? 1 : 0;
}
