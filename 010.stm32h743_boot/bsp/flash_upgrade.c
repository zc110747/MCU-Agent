/**
  ******************************************************************************
  * @file    bsp/flash_upgrade.c
  * @brief   Internal flash upgrade engine (runs from DTCM) + config sector.
  *
  * The erase / program / verify routines live in the .upgrade_ram section
  * (linked into DTCM, loaded from FLASH) so that erasing/programming bank1
  * never stalls the CPU, which is itself executing from bank1 sector 0.
  *
  * Register-level programming (no HAL in the critical path):
  *   - sector erase : CRx.SER + SNB + START, wait BSY
  *   - program      : CRx.PG + 8 x 32-bit stores forming a 256-bit word
  *   - errors       : WRPERR / PGSERR / OPERR / STRBERR / INCERR in SRx
  ******************************************************************************
  */
#include "flash_upgrade.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* section + helpers                                                          */
/* -------------------------------------------------------------------------- */

#define UPGRADE_RAM  __attribute__((section(".upgrade_ram")))

/* FLASH_BANK1_BASE / FLASH_BANK2_BASE are provided by stm32h743xx.h. */

/* Timeouts in DWT cycle-counter ticks (480 MHz). */
#define ERASE_TIMEOUT_CYCLES  (3000000000UL)   /* ~6.2 s per 128 KB sector */
#define PROG_TIMEOUT_CYCLES   (30000000UL)     /* ~62 ms per flash word */

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

/* ---- DWT cycle counter for IRQ-independent timeouts ---- */
static void UPGRADE_RAM dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t UPGRADE_RAM flash_sr(uint32_t bank)
{
    return (bank == 1U) ? FLASH->SR1 : FLASH->SR2;
}

/* Clear all program/erase error flags of a bank. */
static void UPGRADE_RAM flash_clear_errors(uint32_t bank)
{
    uint32_t mask = FLASH_CCR_CLR_WRPERR | FLASH_CCR_CLR_PGSERR |
                    FLASH_CCR_CLR_STRBERR | FLASH_CCR_CLR_INCERR |
                    FLASH_CCR_CLR_OPERR | FLASH_CCR_CLR_EOP;
    if (bank == 1U) FLASH->CCR1 = mask;
    else            FLASH->CCR2 = mask;
}

/* Wait until BSY clears (or timeout); returns 0 ok, -1 busy/error. */
static int UPGRADE_RAM flash_wait_busy(uint32_t bank, uint32_t timeout_cycles)
{
    uint32_t t0 = DWT->CYCCNT;

    while (flash_sr(bank) & FLASH_SR_BSY) {
        if ((DWT->CYCCNT - t0) > timeout_cycles) return -1;
    }

    if (flash_sr(bank) & (FLASH_SR_WRPERR | FLASH_SR_PGSERR |
                          FLASH_SR_STRBERR | FLASH_SR_INCERR | FLASH_SR_OPERR)) {
        return -1;
    }
    return 0;
}

static void UPGRADE_RAM flash_unlock(uint32_t bank)
{
    if (bank == 1U) {
        FLASH->KEYR1 = FLASH_KEY1;
        FLASH->KEYR1 = FLASH_KEY2;
    } else {
        FLASH->KEYR2 = FLASH_KEY1;
        FLASH->KEYR2 = FLASH_KEY2;
    }
}

static uint32_t UPGRADE_RAM flash_bank_of(uint32_t addr)
{
    return (addr < FLASH_BANK2_BASE) ? 1U : 2U;
}

/* Erase one 128 KB sector. Sector is the in-bank number (0..7). */
static int UPGRADE_RAM flash_erase_sector(uint32_t bank, uint32_t sector)
{
    int rc;

    dwt_enable();
    __disable_irq();
    flash_clear_errors(bank);
    if (flash_wait_busy(bank, PROG_TIMEOUT_CYCLES) != 0) { __enable_irq(); return -1; }
    flash_unlock(bank);
    __DSB();

    if (bank == 1U) {
        FLASH->CR1 &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
        FLASH->CR1 |= (FLASH_CR_SER | FLASH_VOLTAGE_RANGE_3 |
                       (sector << FLASH_CR_SNB_Pos) | FLASH_CR_START);
    } else {
        FLASH->CR2 &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
        FLASH->CR2 |= (FLASH_CR_SER | FLASH_VOLTAGE_RANGE_3 |
                       (sector << FLASH_CR_SNB_Pos) | FLASH_CR_START);
    }
    __DSB();

    rc = flash_wait_busy(bank, ERASE_TIMEOUT_CYCLES);

    /* clear SER / SNB; keep the bank locked unless a later op re-unlocks it */
    if (bank == 1U) FLASH->CR1 &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    else            FLASH->CR2 &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    __enable_irq();

    return rc;
}

/* Program one 256-bit flash word (32 bytes at a 32-byte-aligned address). */
static int UPGRADE_RAM flash_program_word(uint32_t addr, const uint32_t *src)
{
    volatile uint32_t *dst = (volatile uint32_t *)addr;
    uint32_t bank = flash_bank_of(addr);
    uint32_t i;
    int rc;

    dwt_enable();
    __disable_irq();
    if (flash_wait_busy(bank, PROG_TIMEOUT_CYCLES) != 0) { __enable_irq(); return -1; }
    flash_unlock(bank);
    __DSB();

    if (bank == 1U) FLASH->CR1 |= FLASH_CR_PG;
    else            FLASH->CR2 |= FLASH_CR_PG;
    __DSB();
    __ISB();

    for (i = 0; i < 8U; i++) dst[i] = src[i];   /* assembles the 256-bit word */
    __DSB();

    rc = flash_wait_busy(bank, PROG_TIMEOUT_CYCLES);

    if (bank == 1U) FLASH->CR1 &= ~FLASH_CR_PG;
    else            FLASH->CR2 &= ~FLASH_CR_PG;
    __enable_irq();

    return rc;
}

/* -------------------------------------------------------------------------- */
/* Public API - all write paths execute from DTCM (.upgrade_ram)              */
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

int UPGRADE_RAM BFLASH_EraseApp(void)
{
    uint32_t s;

    /* bank1 sectors 1..7 */
    for (s = 1U; s <= 7U; s++) {
        if (flash_erase_sector(1U, s) != 0) return -1;
    }
    /* bank2 sectors 0..6 (absolute 8..14) */
    for (s = 0U; s <= 6U; s++) {
        if (flash_erase_sector(2U, s) != 0) return -1;
    }
    return 0;
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
