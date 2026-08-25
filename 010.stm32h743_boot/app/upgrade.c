/**
  ******************************************************************************
  * @file    app/upgrade.c
  * @brief   U-disk (QSPI FAT) firmware upgrade flow.
  *
  * Sequence (all checks before any flash is touched):
  *   1. mount FAT volume (blank flash is formatted)
  *   2. open verify.json, parse name / len / HMAC-SHA256 / version
  *   3. open the package .bin, check f_size == len
  *   4. stream-HMAC the package with the shared key, compare to the JSON value
  *   5. read the current app version at 0x08021000; if equal -> skip
  *      (files are KEPT by design: next reset re-checks and skips)
  *   6. erase only the sectors the app occupies (by app_len; last sector
 *      erased just-in-time right before streaming), stream program + verify
  *   7. write the system config sector (magic/len/version/hmac/status/crc)
  *   8. unmount; the USB MSC stack takes over the raw QSPI
  *
  * If ANY check fails the bootloader aborts before erasing anything, so a bad
  * package can never damage the running app.
  ******************************************************************************
  */
#include "upgrade.h"
#include "fs_init.h"
#include "uart.h"
#include "led.h"
#include "flash_upgrade.h"
#include "flash_secure.h"
#include "hmac_sha256.h"
#include "mini_json.h"
#include "ff.h"
#include <string.h>

#define UPGRADE_JSON_NAME "verify.json"
#define JSON_BUF_SIZE     1024U
#define IO_BUF_SIZE       4096U
#define MIN_APP_LEN       512U

static uint8_t g_json_buf[JSON_BUF_SIZE];
static uint8_t g_io_buf[IO_BUF_SIZE] __attribute__((aligned(32)));

/* -------------------------------------------------------------------------- */
/* small helpers                                                              */
/* -------------------------------------------------------------------------- */

static int hex_char_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_to_bytes(const char *hex, uint8_t *out, uint32_t outlen)
{
    uint32_t i;

    for (i = 0; i < outlen; i++) {
        int hi = hex_char_val(hex[i * 2]);
        int lo = hex_char_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/* "a.b.c.d" -> v[0..3], every component must be 0..99. */
static int parse_version(const char *str, uint8_t v[4])
{
    int parts[4] = {0, 0, 0, 0};
    int idx = 0, val = 0, digit_seen = 0;
    const char *p = str;

    for (;; p++) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            digit_seen = 1;
            if (val > 99) return -1;
        } else if (*p == '.' || *p == '\0') {
            if (!digit_seen) return -1;
            if (idx >= 4) return -1;
            parts[idx++] = val;
            val = 0;
            digit_seen = 0;
            if (*p == '\0') break;
        } else {
            return -1;
        }
    }
    if (idx != 4) return -1;

    for (idx = 0; idx < 4; idx++) v[idx] = (uint8_t)parts[idx];
    return 0;
}

static void log_version(const char *tag, const uint8_t v[4])
{
    BSP_UART_Printf(" %s v%u.%u.%u.%u\r\n", tag, v[0], v[1], v[2], v[3]);
}

/* -------------------------------------------------------------------------- */
/* upgrade flow                                                               */
/* -------------------------------------------------------------------------- */

int BSP_Upgrade_Check(void)
{
    FRESULT fr;
    FIL fjson, fbin;
    UINT br = 0;
    uint32_t jlen, remain;
    long json_len = 0;
    char name[64], hmac_hex[80], ver_str[24];
    uint8_t new_ver[4], cur_ver[4];
    uint8_t digest[32], expect[32];
    HMAC_SHA256_CTX hctx;
    uint32_t total;

    if (FS_Mount() != 0) {
        BSP_UART_Printf("[UPG] FAT mount failed - skip upgrade check\r\n");
        return -1;
    }

    BSP_UART_Printf("[UPG] scanning QSPI volume for upgrade package...\r\n");

    /* ---- 1. verify.json ---- */
    fr = f_open(&fjson, UPGRADE_JSON_NAME, FA_READ);
    if (fr != FR_OK) {
        BSP_UART_Printf("[UPG] no %s (0x%02X) - nothing to upgrade\r\n",
                        UPGRADE_JSON_NAME, (unsigned)fr);
        FS_Unmount();
        return 0;
    }

    jlen = f_size(&fjson);
    if (jlen == 0 || jlen >= JSON_BUF_SIZE) {
        BSP_UART_Printf("[UPG] %s size %lu invalid\r\n", UPGRADE_JSON_NAME, (unsigned long)jlen);
        f_close(&fjson);
        FS_Unmount();
        return -1;
    }
    fr = f_read(&fjson, g_json_buf, jlen, &br);
    f_close(&fjson);
    if (fr != FR_OK || br != jlen) {
        BSP_UART_Printf("[UPG] %s read FAIL (0x%02X)\r\n", UPGRADE_JSON_NAME, (unsigned)fr);
        FS_Unmount();
        return -1;
    }
    g_json_buf[br] = '\0';

    if (mjson_get_string((const char *)g_json_buf, "name", name, sizeof(name)) != 0 ||
        mjson_get_int((const char *)g_json_buf, "len", &json_len) != 0 ||
        mjson_get_string((const char *)g_json_buf, "HMAC-SHA256", hmac_hex, sizeof(hmac_hex)) != 0 ||
        mjson_get_string((const char *)g_json_buf, "version", ver_str, sizeof(ver_str)) != 0) {
        BSP_UART_Printf("[UPG] verify.json fields missing/invalid\r\n");
        FS_Unmount();
        return -1;
    }
    if (parse_version(ver_str, new_ver) != 0) {
        BSP_UART_Printf("[UPG] bad version string '%s'\r\n", ver_str);
        FS_Unmount();
        return -1;
    }
    if (hex_to_bytes(hmac_hex, expect, 32) != 0) {
        BSP_UART_Printf("[UPG] bad HMAC-SHA256 string\r\n");
        FS_Unmount();
        return -1;
    }
    if (json_len < (long)MIN_APP_LEN || json_len > (long)APP_SIZE) {
        BSP_UART_Printf("[UPG] len %ld out of range [%u, %lu]\r\n",
                        json_len, MIN_APP_LEN, (unsigned long)APP_SIZE);
        FS_Unmount();
        return -1;
    }

    BSP_UART_Printf("[UPG] package: %s, %ld bytes, ", name, json_len);
    log_version("new", new_ver);

    /* ---- 2. package file ---- */
    fr = f_open(&fbin, name, FA_READ);
    if (fr != FR_OK) {
        BSP_UART_Printf("[UPG] package '%s' missing (0x%02X)\r\n", name, (unsigned)fr);
        FS_Unmount();
        return -1;
    }
    if ((long)f_size(&fbin) != json_len) {
        BSP_UART_Printf("[UPG] package size mismatch (%lu != %ld)\r\n",
                        (unsigned long)f_size(&fbin), json_len);
        f_close(&fbin);
        FS_Unmount();
        return -1;
    }

    /* ---- 3. HMAC-SHA256 over the whole package ---- */
    hmac_sha256_init(&hctx, BOOT_HMAC_KEY, BOOT_HMAC_KEY_LEN);
    remain = (uint32_t)json_len;
    while (remain > 0) {
        UINT want = (remain > IO_BUF_SIZE) ? IO_BUF_SIZE : remain;
        fr = f_read(&fbin, g_io_buf, want, &br);
        if (fr != FR_OK || br != want) {
            BSP_UART_Printf("[UPG] read FAIL (0x%02X) @ %lu\r\n", (unsigned)fr,
                            (unsigned long)(json_len - remain));
            f_close(&fbin);
            FS_Unmount();
            return -1;
        }
        hmac_sha256_update(&hctx, g_io_buf, br);
        remain -= br;
    }
    hmac_sha256_final(&hctx, digest);

    if (memcmp(digest, expect, 32) != 0) {
        BSP_UART_Printf("[UPG] HMAC-SHA256 MISMATCH - package rejected\r\n");
        f_close(&fbin);
        FS_Unmount();
        return -1;
    }
    BSP_UART_Printf("[UPG] HMAC-SHA256 verified OK\r\n");

    /* ---- 4. version check (upgrade allowed on ANY difference, incl. downgrade) ---- */
    BFLASH_AppVersionRead(cur_ver);
    log_version("cur ", cur_ver);
    if (memcmp(cur_ver, new_ver, 4) == 0) {
        BSP_UART_Printf("[UPG] version identical - skip upgrade (package kept)\r\n");
        f_close(&fbin);
        FS_Unmount();
        return 0;
    }

    /* ---- 5. erase app region (engine runs from DTCM) ---- */
    {
        uint32_t first_sec = (APP_BASE_ADDR - FLASH_BASE) / FLASH_SECTOR_SIZE;
        uint32_t last_sec  = (APP_BASE_ADDR + (uint32_t)json_len - 1U - FLASH_BASE) / FLASH_SECTOR_SIZE;
        BSP_UART_Printf("[UPG] erasing %lu sector(s) by app length (%ld B) ...\r\n",
                        (unsigned long)(last_sec - first_sec + 1U), json_len);
    }
    /* front blocks derived from app_len */
    if (BFLASH_EraseApp((uint32_t)json_len) != 0) {
        BSP_UART_Printf("[UPG] erase FAILED\r\n");
        f_close(&fbin);
        FS_Unmount();
        return -1;
    }
    /* last block wiped just-in-time, right before the upgrade stream writes */
    if (BFLASH_EraseAppLastSector((uint32_t)json_len) != 0) {
        BSP_UART_Printf("[UPG] last-sector erase FAILED\r\n");
        f_close(&fbin);
        FS_Unmount();
        return -1;
    }
    BSP_UART_Printf("[UPG] erase done\r\n");

    /* ---- 6. stream program + read-back verify ---- */
    if (f_lseek(&fbin, 0) != FR_OK) {
        f_close(&fbin);
        FS_Unmount();
        return -1;
    }
    total = 0;
    while (total < (uint32_t)json_len) {
        UINT want = ((uint32_t)json_len - total > IO_BUF_SIZE) ? IO_BUF_SIZE
                                                               : ((uint32_t)json_len - total);
        fr = f_read(&fbin, g_io_buf, want, &br);
        if (fr != FR_OK || br != want) {
            BSP_UART_Printf("[UPG] read FAIL (0x%02X) @ %lu\r\n", (unsigned)fr,
                            (unsigned long)total);
            f_close(&fbin);
            FS_Unmount();
            return -1;
        }

        /* pad the last chunk to a 256-bit flash word with 0xFF (erased state) */
        uint32_t chunk = br;
        if ((chunk & (FLASH_WORD_SIZE - 1U)) != 0U) {
            memset(&g_io_buf[chunk], 0xFF, FLASH_WORD_SIZE - (chunk & (FLASH_WORD_SIZE - 1U)));
            chunk += FLASH_WORD_SIZE - (chunk & (FLASH_WORD_SIZE - 1U));
        }

        if (BFLASH_ProgramBlock(APP_BASE_ADDR + total, g_io_buf, chunk) != 0) {
            BSP_UART_Printf("[UPG] program FAIL @ 0x%08lX\r\n",
                            (unsigned long)(APP_BASE_ADDR + total));
            f_close(&fbin);
            FS_Unmount();
            return -1;
        }
        if (BFLASH_VerifyBlock(APP_BASE_ADDR + total, g_io_buf, chunk) != 0) {
            BSP_UART_Printf("[UPG] read-back verify FAIL @ 0x%08lX\r\n",
                            (unsigned long)(APP_BASE_ADDR + total));
            f_close(&fbin);
            FS_Unmount();
            return -1;
        }

        total += br;
        if ((total & 0x3FFFFUL) == 0UL || total >= (uint32_t)json_len) {
            BSP_UART_Printf("[UPG]   ... programmed %lu / %lu bytes\r\n",
                            (unsigned long)total, (unsigned long)json_len);
            BSP_LED_Toggle();
        }
    }
    BSP_UART_Printf("[UPG] program + verify done\r\n");

    /* ---- 7. update system config sector ---- */
    {
        app_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic   = APP_CFG_MAGIC;
        cfg.app_len = (uint32_t)json_len;
        memcpy(cfg.version, new_ver, 4);
        memcpy(cfg.app_hmac, digest, 32);
        cfg.status  = APP_CFG_STATUS_OK;
        cfg.crc32   = BFLASH_ConfigCrc(&cfg);

        if (BFLASH_ConfigWrite(&cfg) != 0) {
            BSP_UART_Printf("[UPG] system config write FAILED\r\n");
            f_close(&fbin);
            FS_Unmount();
            return -1;
        }
        BSP_UART_Printf("[UPG] system config updated\r\n");
    }

    f_close(&fbin);
    FS_Unmount();
    BSP_UART_Printf("[UPG] upgrade SUCCESS (package files kept on disk)\r\n");
    return 1;
}
