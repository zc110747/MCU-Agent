/**
  ******************************************************************************
  * @file    bsp/flash_upgrade.h
  * @brief   Internal flash upgrade engine + system configuration sector.
  *
  * Internal flash layout (STM32H743ZIT6, 2 MB, 16 sectors x 128 KB):
  *   0x08000000 - 0x0801FFFF : bootloader (this image)
  *   0x08020000 - 0x081DFFFF : app (sectors 1..14)
  *   0x08021000              : app version, 4 bytes (major,minor,patch,build)
  *   0x081E0000 - 0x081FFFFF : system config sector (sector 15)
  *
  * Erase/program/verify routines are placed in .upgrade_ram (DTCM, loaded
  * from FLASH): while bank1 is being erased/programmed the CPU must not
  * fetch instructions from bank1, so the write engine executes from RAM.
  * Call BFLASH_Relocate() once before using any of these functions.
  ******************************************************************************
  */
#ifndef __BSP_FLASH_UPGRADE_H
#define __BSP_FLASH_UPGRADE_H

#include "stm32h7xx_hal.h"

/* ---- Internal flash layout ---- */
#define APP_BASE_ADDR       0x08020000UL
#define APP_SIZE            0x001C0000UL       /* sectors 1..14 */
#define APP_VERSION_ADDR    0x08021000UL       /* 4 bytes */
#define APP_VERSION_OFFSET  (APP_VERSION_ADDR - APP_BASE_ADDR)
#define CFG_BASE_ADDR       0x081E0000UL       /* sector 15 */
#define CFG_SIZE            0x00020000UL
#define FLASH_SECTOR_SIZE   0x00020000UL       /* 128 KB per H743 sector */
#define FLASH_WORD_SIZE     32U                /* 256-bit flash word */

/* ---- System configuration sector ---- */
#define APP_CFG_MAGIC       0xB0075EEDUL
#define APP_CFG_STATUS_OK   0x00000001UL

typedef struct {
    uint32_t magic;          /* +0   APP_CFG_MAGIC                      */
    uint32_t app_len;        /* +4   programmed app image length (B)    */
    uint8_t  version[4];     /* +8   major, minor, patch, build (0..99) */
    uint8_t  app_hmac[32];   /* +12  HMAC-SHA256 over app image         */
    uint32_t status;         /* +44  status flags                       */
    uint32_t crc32;          /* +48  CRC32 of bytes [0, 48) (excl. self) */
    uint8_t  reserved[12];   /* +52..63                                */
} app_config_t;              /* 64 bytes = 2 flash words                */

/* ---- API ---- */
void BFLASH_Relocate(void);                              /* copy engine to DTCM */
int  BFLASH_EraseApp(void);                              /* erase sectors 1..14 */
int  BFLASH_ProgramBlock(uint32_t addr, const uint8_t *src, uint32_t len);
int  BFLASH_VerifyBlock(uint32_t addr, const uint8_t *src, uint32_t len);
int  BFLASH_ConfigRead(app_config_t *cfg);
int  BFLASH_ConfigWrite(const app_config_t *cfg);
void BFLASH_AppVersionRead(uint8_t v[4]);
int  BFLASH_AppVectorValid(void);
uint32_t BFLASH_ConfigCrc(const app_config_t *cfg);

#endif /* __BSP_FLASH_UPGRADE_H */
