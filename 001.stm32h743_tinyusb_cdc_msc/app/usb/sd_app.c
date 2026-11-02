/* ---------------------------------------------------------------------------
* FatFs application layer (see sd_app.h)
* -------------------------------------------------------------------------*/

#include "sd_app.h"
#include "bsp_sdcard.h"
#include "bsp_sdcache.h"
#include "ff.h"
#include "tusb.h"

#include <stdarg.h>
#include <string.h>
#include <stdio.h>      /* vsnprintf */

typedef struct
{
    FATFS   fs;
    bool    mounted;
    bool    host;
    uint8_t work[4096];
} SD_APP_STATE_T;

static SD_APP_STATE_T g_sd_app = {0};

/* FatFs logical drive that maps to the SD card.
 *
 * FF_VOLUMES is 2 and third_party/FatFs/diskio.c assigns physical drives as
 * 0 = RAM disk (DEV_RAM), 1 = SD/MMC (DEV_MMC).
 *
 * Every path passed to FatFs MUST carry this prefix. With FF_FS_RPATH == 0 the
 * volume parser resolves a path without a drive number to drive 0 uncondition-
 * ally (get_ldnumber() returns 0), and f_chdrive() has no effect at all because
 * CurrVol is only consulted when FF_FS_RPATH == 1. So a bare "/" or "README.TXT"
 * silently addressed the unformatted RAM disk at 0x24000000 even though the SD
 * card had mounted fine - which is exactly why ls/cat kept failing with
 * FR_NO_PATH while "FS mount" said yes. */
#define SD_VOL "1:"

/* ---- tiny, unbuffered CDC output (newlib printf would buffer) ------------ */
static void sd_write(const void* data, uint32_t len)
{
    uint32_t sent = 0;
    while (sent < len)
    {
        if (!tud_cdc_connected()) break;
        uint32_t n = tud_cdc_write((const uint8_t*) data + sent, len - sent);
        if (n == 0) break;
        sent += n;
        tud_cdc_write_flush();
    }
}

static void sd_puts(const char* s)
{
    sd_write(s, (uint32_t) strlen(s));
}

static void sd_printf(const char* fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int32_t n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) sd_write(buf, (uint32_t) n);
}

/* ------------------------------------------------------------------------ */
void fatfs_init(void)
{
    FRESULT fr = f_mount(&g_sd_app.fs, SD_VOL, 1);
    if (fr == FR_NO_FILESYSTEM)
    {
        /* First run on a blank card: lay down a FAT32 volume and drop a README. */
        MKFS_PARM opt = { FM_FAT32, 0, 0, 0, 0 };
        fr = f_mkfs(SD_VOL, &opt, g_sd_app.work, sizeof(g_sd_app.work));
        if (fr == FR_OK)
        {
            f_mount(NULL, SD_VOL, 0);                 /* unmount, then re-mount clean */
            fr = f_mount(&g_sd_app.fs, SD_VOL, 1);
            if (fr == FR_OK)
            {
                FIL f;
                if (f_open(&f, SD_VOL "/README.TXT", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
                {
                    const char* msg =
                    "STM32H743 TinyUSB CDC + MSC demo\r\n"
                    "This SD card is exported to the PC as a USB mass-storage device.\r\n"
                    "The CDC serial port can list/read files via 'ls' and 'cat'.\r\n";
                    UINT bw = 0;
                    f_write(&f, msg, (UINT) strlen(msg), &bw);
                    f_close(&f);
                }
            }
        }
    }
    g_sd_app.mounted = (fr == FR_OK);
    sd_trace_add(SD_TR_FATFS, (uint8_t) fr, 0, 0);
}

void fatfs_release_for_host(void)
{
    if (g_sd_app.mounted)
    {
        f_mount(NULL, SD_VOL, 0); g_sd_app.mounted = false;
    }
    g_sd_app.host = true;
}

void fatfs_reacquire(void)
{
    g_sd_app.host = false;
    if (!g_sd_app.mounted)
    {
        FRESULT fr = f_mount(&g_sd_app.fs, SD_VOL, 1);
        g_sd_app.mounted = (fr == FR_OK);
    }
}

bool fatfs_host_active(void)
{
    return g_sd_app.host;
}

/* ------------------------------------------------------------------------ */
void cmd_sd(void)
{
    sd_printf("SD card on SDMMC1\r\n");
    if (!sdcard_present())
    {
        sd_printf("  not detected\r\n"); return;
    }
    uint64_t bytes = (uint64_t) sdcard_block_count() * sdcard_block_size();
    sd_printf("  capacity : %lu blocks x %lu B = %lu MB\r\n",
    (unsigned long) sdcard_block_count(),
    (unsigned long) sdcard_block_size(),
    (unsigned long) (bytes / (1024UL * 1024UL)));
    sd_printf("  FS mount : %s\r\n", g_sd_app.mounted ? "yes" : "no");
    sd_printf("  host use : %s\r\n", g_sd_app.host ? "yes - device file ops blocked" : "no");

    SDCACHE_STATS_T c;
    sdcache_stats(&c);
    sd_printf("  blk cache: %lu/%lu sectors cached, %lu dirty (see 'scache')\r\n",
              (unsigned long) c.valid,
              (unsigned long) c.sectors,
              (unsigned long) c.dirty);
}

void cmd_ls(void)
{
    /* Read-only: safe even while the USB host owns the card (the device never
    * writes here). Only concurrent *writes* can corrupt the FS, which the
    * g_sd_app.host hand-off prevents by releasing/reacquiring the FatFs mount. */
    if (!g_sd_app.mounted)
    {
        sd_printf("SD not mounted.\r\n"); return;
    }

    DIR dir; FILINFO fno;
    FRESULT fr = f_opendir(&dir, SD_VOL "/");
    if (fr != FR_OK)
    {
        sd_printf("opendir failed (fr=%d, fs_type=%d)\r\n", (int) fr, (int) g_sd_app.fs.fs_type);
        return;
    }
    sd_printf("  size     name\r\n");
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0])
    {
        sd_printf("  %8lu  %s\r\n", (unsigned long) fno.fsize, fno.fname);
    }
    f_closedir(&dir);
}

void cmd_cat(const char* fname)
{
    /* Read-only: safe even while the USB host owns the card (see cmd_ls). */
    if (!g_sd_app.mounted)
    {
        sd_printf("SD not mounted.\r\n"); return;
    }

    FIL f;
    /* The drive prefix is mandatory (see SD_VOL). Accept both "README.TXT" and
       "/README.TXT" from the user, but never emit a doubled separator. */
    char path[80];
    const char* rel = fname;
    while (*rel == '/' || *rel == '\\') rel++;
    snprintf(path, sizeof(path), SD_VOL "/%s", rel);

    FRESULT fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK)
    {
        sd_printf("cannot open '%s' (fr=%d)\r\n", path, (int) fr);
        return;
    }
    uint8_t buf[64]; UINT br;
    while (f_read(&f, buf, sizeof(buf), &br) == FR_OK && br)
    {
        sd_write(buf, br);
    }
    f_close(&f);
    sd_puts("\r\n");
}

void cmd_remount(void)
{
    if (g_sd_app.mounted) f_mount(NULL, SD_VOL, 0);
    FRESULT fr = f_mount(&g_sd_app.fs, SD_VOL, 1);
    g_sd_app.mounted = (fr == FR_OK);
    sd_printf("FatFs remounted: %s\r\n", g_sd_app.mounted ? "ok" : "fail");
}

/* Force the write-back window to the card. The same thing happens on a host
 * eject, on SCSI SYNCHRONIZE CACHE and on FatFs CTRL_SYNC (f_sync/f_close);
 * this command exists so the behaviour can be observed from the CDC side. */
void cmd_sync(void)
{
    uint32_t const before = sdcache_dirty();
    uint32_t const t0 = board_millis();
    int32_t  const st = sdcache_flush();
    uint32_t const dt = board_millis() - t0;

    sd_printf("cache flush: %s  dirty %lu -> %lu  %lu ms\r\n",
              (st == SD_ST_OK) ? "ok" : "FAILED",
              (unsigned long) before,
              (unsigned long) sdcache_dirty(),
              (unsigned long) dt);
}

void cmd_scache(void)
{
    SDCACHE_STATS_T s;
    sdcache_stats(&s);

    uint32_t const total = s.rd_hit + s.rd_miss + s.rd_direct;
    uint32_t const hit_pct = (total == 0u) ? 0u : (100u * s.rd_hit) / total;

    sd_printf("SD block cache (write-back)\r\n");
    sd_printf("  window   : %lu KB = %lu sectors, %lu cached / %lu dirty\r\n",
              (unsigned long) (SDCACHE_BYTES / 1024u),
              (unsigned long) s.sectors,
              (unsigned long) s.valid,
              (unsigned long) s.dirty);
    sd_printf("  read     : %lu hit  %lu fill  %lu direct  (%lu%% hit)\r\n",
              (unsigned long) s.rd_hit,
              (unsigned long) s.rd_miss,
              (unsigned long) s.rd_direct,
              (unsigned long) hit_pct);
    sd_printf("  write    : %lu hit  %lu buffered  %lu direct\r\n",
              (unsigned long) s.wr_hit,
              (unsigned long) s.wr_miss,
              (unsigned long) s.wr_direct);
    sd_printf("  writeback: %lu ops, %lu sectors, %lu dirty evictions, "
              "%lu drops\r\n",
              (unsigned long) s.wb_ops,
              (unsigned long) s.wb_sectors,
              (unsigned long) s.evict_dirty,
              (unsigned long) s.dropped);
    sd_printf("  prewarm  : %lu sectors (system %lu, fat %lu), status %ld\r\n",
              (unsigned long) s.prewarm_secs,
              (unsigned long) s.prewarm_sys,
              (unsigned long) s.prewarm_fat,
              (long) s.prewarm_st);

    if (s.fs_type != 0u)
    {
        sd_printf("  layout   : FAT%u  part@%lu  rsvd %lu  fatsz %lu  "
                  "root %lu  spc %lu\r\n",
                  (unsigned) s.fs_type,
                  (unsigned long) s.part_lba,
                  (unsigned long) s.rsvd,
                  (unsigned long) s.fatsz,
                  (unsigned long) s.root_secs,
                  (unsigned long) s.sec_per_clus);
    }
    else
    {
        sd_printf("  layout   : not recognised (blank or unsupported)\r\n");
    }
}
