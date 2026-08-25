/**
  ******************************************************************************
  * @file    bsp/flash_upgrade.c
  * @brief   Internal flash upgrade engine (runs from AXI SRAM) + config sector.
  *
  * RWW note (STM32H7): while a bank is being erased/programmed, code fetches
  * from that same bank are only STALLED (not faulted) by the flash interface.
  * The erase / program / verify routines therefore live in the .upgrade_ram
  * section (AXI SRAM 0x24000000, loaded from FLASH) so the write engine never
  * competes with the CPU for bank1 fetches - this keeps the upgrade path free
  * of RWW stalls and removes any coherency hazard.
  *
  * The actual erase/program is performed through the verified HAL primitives
  * (HAL_FLASHEx_Erase / HAL_FLASH_Program with FLASH_TYPEPROGRAM_FLASHWORD),
  * identical to the known-good reference implementation. All error flags of
  * both banks are cleared before every operation, and the flash is unlocked
  * for the duration of the operation with IRQs disabled.
  ******************************************************************************
  */
#include "flash_upgrade.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* section + helpers                                                          */
/* -------------------------------------------------------------------------- */

#define UPGRADE_RAM  __attribute__((section(".upgrade_ram")))

/* FLASH_BANK1_BASE / FLASH_BANK2_BASE are provided by stm32h743xx.h. */

/* ---- CRC32 (IEEE 802.3, reflected, poly 0xEDB88320) ---- */
static uint32_t crc32_bytes(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    while (len--) {
        crc ^= *data++;
        for (i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return crc;
}

uint32_t BFLASH_ConfigCrc(const app_config_t *cfg)
{
    /* CRC over all fields EXCEPT the trailing crc32 word (bytes [0, 48)).
       Including the crc32 field itself would make the stored value differ
       between write (field == 0) and read (field == stored), breaking the
       check. */
    return crc32_bytes(0xFFFFFFFFUL, (const uint8_t *)cfg, 48U) ^ 0xFFFFFFFFUL;
}

/* Clear all program/erase error flags of BOTH banks. Must run before every
   flash operation so a stale flag from a previous (failed) op cannot block the
   next one. */
static void UPGRADE_RAM flash_clear_all_errors(void)
{
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1);
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);
}

/* -------------------------------------------------------------------------- */
/* Low-level engine (AXI SRAM) - wraps the verified HAL primitives             */
/* -------------------------------------------------------------------------- */

/* Erase one 128 KB in-bank sector. */
static int UPGRADE_RAM flash_erase_sector(uint32_t bank, uint32_t sector)
{
    FLASH_EraseInitTypeDef ei;
    uint32_t sector_error = 0U;
    HAL_StatusTypeDef st;

    __disable_irq();
    HAL_FLASH_Unlock();
    flash_clear_all_errors();

    ei.TypeErase    = FLASH_TYPEERASE_SECTORS;
    ei.Banks        = (bank == 1U) ? FLASH_BANK_1 : FLASH_BANK_2;
    ei.Sector       = sector;
    ei.NbSectors    = 1U;
    ei.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    st = HAL_FLASHEx_Erase(&ei, &sector_error);

    HAL_FLASH_Lock();
    __enable_irq();

    return (st == HAL_OK) ? 0 : -1;
}

/* Program one 256-bit flash word (32 bytes at a 32-byte-aligned address).
   src points at a 32-bit-aligned, 32-byte buffer holding the new contents. */
static int UPGRADE_RAM flash_program_word(uint32_t addr, const uint32_t *src)
{
    HAL_StatusTypeDef st;

    __disable_irq();
    HAL_FLASH_Unlock();
    flash_clear_all_errors();

    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addr, (uint32_t)src);

    HAL_FLASH_Lock();
    __enable_irq();

    return (st == HAL_OK) ? 0 : -1;
}

/* -------------------------------------------------------------------------- */
/* Public API - all write paths execute from AXI SRAM (.upgrade_ram)           */
/* -------------------------------------------------------------------------- */

void BFLASH_Relocate(void)
{
    extern uint8_t _supgrade_ram, _eupgrade_ram, _supgrade_ram_load;
    uint32_t size = (uint32_t)&_eupgrade_ram - (uint32_t)&_supgrade_ram;

    if (size == 0U) return;

    memcpy(&_supgrade_ram, &_supgrade_ram_load, size);
    __DSB();
    __ISB();   /* make the copied code visible before any call into it */
}

/* Map a flash address to (bank, in-bank sector). H743: 16 x 128 KB sectors,
   bank1 = absolute sectors 0..7 (0x08000000..0x080FFFFF),
   bank2 = absolute sectors 8..15 (0x08100000..0x081FFFFF). */
static void UPGRADE_RAM addr_to_bank_sector(uint32_t addr, uint32_t *bank, uint32_t *sector)
{
    uint32_t off = addr - FLASH_BASE;
    uint32_t sec = off / FLASH_SECTOR_SIZE;     /* 0..15 absolute */

    if (sec >= 8U) { *bank = 2U; *sector = sec - 8U; }
    else           { *bank = 1U; *sector = sec; }
}

/* Erase every sector the app occupies EXCEPT the last one. The last sector is
   erased separately right before the upgrade writes into it (see
   BFLASH_EraseAppLastSector), so the bulk upfront erase only covers the front
   blocks derived from app_len. A zero length is treated as one flash word so it
   can never wipe the whole region. */
int UPGRADE_RAM BFLASH_EraseApp(uint32_t app_len)
{
    uint32_t total     = app_len ? app_len : FLASH_WORD_SIZE;
    uint32_t first_sec = (APP_BASE_ADDR - FLASH_BASE) / FLASH_SECTOR_SIZE;
    uint32_t last_sec  = (APP_BASE_ADDR + total - 1U - FLASH_BASE) / FLASH_SECTOR_SIZE;
    uint32_t s;

    if (last_sec <= first_sec) {
        return 0;   /* single sector: nothing upfront, last block erased later */
    }
    for (s = first_sec; s < last_sec; s++) {
        uint32_t bank, sector;
        addr_to_bank_sector(FLASH_BASE + s * FLASH_SECTOR_SIZE, &bank, &sector);
        if (flash_erase_sector(bank, sector) != 0) return -1;
    }
    return 0;
}

/* Erase only the last sector the app occupies. Called just before the upgrade
   stream-program starts, so the final block is wiped just-in-time rather than
   in the upfront bulk erase. */
int UPGRADE_RAM BFLASH_EraseAppLastSector(uint32_t app_len)
{
    uint32_t total    = app_len ? app_len : FLASH_WORD_SIZE;
    uint32_t last_sec = (APP_BASE_ADDR + total - 1U - FLASH_BASE) / FLASH_SECTOR_SIZE;
    uint32_t bank, sector;

    addr_to_bank_sector(FLASH_BASE + last_sec * FLASH_SECTOR_SIZE, &bank, &sector);
    return flash_erase_sector(bank, sector);
}

int UPGRADE_RAM BFLASH_ProgramBlock(uint32_t addr, const uint8_t *src, uint32_t len)
{
    const uint32_t *sp = (const uint32_t *)src;

    if ((addr & (FLASH_WORD_SIZE - 1U)) != 0U) return -1;
    if ((len & (FLASH_WORD_SIZE - 1U)) != 0U) return -1;

    while (len > 0U) {
        if (flash_program_word(addr, sp) != 0) return -1;
        addr += FLASH_WORD_SIZE;
        sp   += FLASH_WORD_SIZE / 4U;
        len  -= FLASH_WORD_SIZE;
    }
    return 0;
}

int UPGRADE_RAM BFLASH_VerifyBlock(uint32_t addr, const uint8_t *src, uint32_t len)
{
    const volatile uint8_t *fa = (const volatile uint8_t *)addr;
    uint32_t i;

    for (i = 0; i < len; i++) {
        if (fa[i] != src[i]) return -1;
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* System config sector (flash-resident; bank2 ops are RWW-safe)              */
/* -------------------------------------------------------------------------- */

int BFLASH_ConfigRead(app_config_t *cfg)
{
    if (cfg == NULL) return -1;

    memcpy(cfg, (const void *)CFG_BASE_ADDR, sizeof(*cfg));

    if (cfg->magic != APP_CFG_MAGIC) return -1;
    if (cfg->crc32 != BFLASH_ConfigCrc(cfg)) return -1;
    return 0;
}

int BFLASH_ConfigWrite(const app_config_t *cfg)
{
    app_config_t tmp __attribute__((aligned(32)));
    uint32_t crc;

    if (cfg == NULL) return -1;

    memcpy(&tmp, cfg, sizeof(tmp));
    crc = BFLASH_ConfigCrc(&tmp);
    tmp.crc32 = crc;

    /* sector 15 = bank2 sector 7 */
    if (flash_erase_sector(2U, 7U) != 0) return -1;
    if (BFLASH_ProgramBlock(CFG_BASE_ADDR, (const uint8_t *)&tmp, sizeof(tmp)) != 0) return -1;
    if (BFLASH_VerifyBlock(CFG_BASE_ADDR, (const uint8_t *)&tmp, sizeof(tmp)) != 0) return -1;

    return 0;
}

void BFLASH_AppVersionRead(uint8_t v[4])
{
    const uint8_t *p = (const uint8_t *)APP_VERSION_ADDR;
    uint32_t i;

    for (i = 0; i < 4U; i++) v[i] = p[i];
}

int BFLASH_AppVectorValid(void)
{
    uint32_t sp    = *(const volatile uint32_t *)APP_BASE_ADDR;
    uint32_t reset = *(const volatile uint32_t *)(APP_BASE_ADDR + 4U);

    /* Initial SP may point at the very top of a RAM region (full-descending
       stack), so the upper bound is inclusive. Accept every internal SRAM the
       H743 exposes, so real apps are not rejected for using SRAM1/2/3/4. */
    int ok = 0;
    if (sp >= 0x20000000UL && sp <= 0x20020000UL) ok = 1;   /* DTCM 128K */
    if (sp >= 0x24000000UL && sp <= 0x24080000UL) ok = 1;   /* AXI SRAM 512K */
    if (sp >= 0x30000000UL && sp <= 0x30020000UL) ok = 1;   /* SRAM1 128K */
    if (sp >= 0x30020000UL && sp <= 0x30040000UL) ok = 1;   /* SRAM2 128K */
    if (sp >= 0x30040000UL && sp <= 0x30048000UL) ok = 1;   /* SRAM3 32K */
    if (sp >= 0x38000000UL && sp <= 0x38010000UL) ok = 1;   /* SRAM4 64K */
    if (sp >= 0x38800000UL && sp <= 0x38801000UL) ok = 1;   /* Backup 4K */
    if (!ok) return 0;

    if ((sp & 0x7UL) != 0UL) return 0;          /* 8-byte aligned */
    if (reset < APP_BASE_ADDR || reset >= APP_BASE_ADDR + APP_SIZE) return 0;

    return 1;
}
