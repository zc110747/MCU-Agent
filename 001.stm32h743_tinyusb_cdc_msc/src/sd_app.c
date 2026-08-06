/* ---------------------------------------------------------------------------
 * FatFs application layer (see sd_app.h)
 * -------------------------------------------------------------------------*/

#include "sd_app.h"
#include "sdcard.h"
#include "ff.h"
#include "tusb.h"

#include <stdarg.h>
#include <string.h>
#include <stdio.h>      /* vsnprintf */

static FATFS   g_fs;
static bool    g_mounted = false;   /* device-side FatFs currently mounted */
static bool    g_host    = false;   /* USB host currently owns the card    */
static uint8_t g_work[4096];        /* scratch buffer for f_mkfs           */

/* ---- tiny, unbuffered CDC output (newlib printf would buffer) ------------ */
static void sd_write(const void* data, uint32_t len) {
  uint32_t sent = 0;
  while (sent < len) {
    if (!tud_cdc_connected()) break;
    uint32_t n = tud_cdc_write((const uint8_t*) data + sent, len - sent);
    if (n == 0) break;
    sent += n;
    tud_cdc_write_flush();
  }
}

static void sd_puts(const char* s) { sd_write(s, (uint32_t) strlen(s)); }

static void sd_printf(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) sd_write(buf, (uint32_t) n);
}

/* ------------------------------------------------------------------------ */
void fatfs_init(void) {
  FRESULT fr = f_mount(&g_fs, "0:", 1);
  if (fr == FR_NO_FILESYSTEM) {
    /* First run on a blank card: lay down a FAT32 volume and drop a README. */
    MKFS_PARM opt = { FM_FAT32, 0, 0, 0, 0 };
    fr = f_mkfs("0:", &opt, g_work, sizeof(g_work));
    if (fr == FR_OK) {
      f_mount(NULL, "0:", 0);                 /* unmount, then re-mount clean */
      fr = f_mount(&g_fs, "0:", 1);
      if (fr == FR_OK) {
        FIL f;
        if (f_open(&f, "README.TXT", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
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
  g_mounted = (fr == FR_OK);
}

void fatfs_release_for_host(void) {
  if (g_mounted) { f_mount(NULL, "0:", 0); g_mounted = false; }
  g_host = true;
}

void fatfs_reacquire(void) {
  g_host = false;
  if (!g_mounted) {
    FRESULT fr = f_mount(&g_fs, "0:", 1);
    g_mounted = (fr == FR_OK);
  }
}

bool fatfs_host_active(void) { return g_host; }

/* ------------------------------------------------------------------------ */
void cmd_sd(void) {
  sd_printf("SD card on SDMMC1\r\n");
  if (!sdcard_present()) { sd_printf("  not detected\r\n"); return; }
  uint64_t bytes = (uint64_t) sdcard_block_count() * sdcard_block_size();
  sd_printf("  capacity : %lu blocks x %lu B = %lu MB\r\n",
            (unsigned long) sdcard_block_count(),
            (unsigned long) sdcard_block_size(),
            (unsigned long) (bytes / (1024UL * 1024UL)));
  sd_printf("  FS mount : %s\r\n", g_mounted ? "yes" : "no");
  sd_printf("  host use : %s\r\n", g_host ? "yes - device file ops blocked" : "no");
}

void cmd_ls(void) {
  /* Read-only: safe even while the USB host owns the card (the device never
   * writes here). Only concurrent *writes* can corrupt the FS, which the
   * g_host hand-off prevents by releasing/reacquiring the FatFs mount. */
  if (!g_mounted) { sd_printf("SD not mounted.\r\n"); return; }

  DIR dir; FILINFO fno;
  if (f_opendir(&dir, "/") != FR_OK) { sd_printf("opendir failed\r\n"); return; }
  sd_printf("  size     name\r\n");
  while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
    sd_printf("  %8lu  %s\r\n", (unsigned long) fno.fsize, fno.fname);
  }
  f_closedir(&dir);
}

void cmd_cat(const char* fname) {
  /* Read-only: safe even while the USB host owns the card (see cmd_ls). */
  if (!g_mounted) { sd_printf("SD not mounted.\r\n"); return; }

  FIL f;
  if (f_open(&f, fname, FA_READ) != FR_OK) {
    sd_printf("cannot open '%s'\r\n", fname);
    return;
  }
  char buf[64]; UINT br;
  while (f_read(&f, buf, sizeof(buf), &br) == FR_OK && br) {
    sd_write(buf, br);
  }
  f_close(&f);
  sd_puts("\r\n");
}

void cmd_remount(void) {
  if (g_mounted) f_mount(NULL, "0:", 0);
  FRESULT fr = f_mount(&g_fs, "0:", 1);
  g_mounted = (fr == FR_OK);
  sd_printf("FatFs remounted: %s\r\n", g_mounted ? "ok" : "fail");
}
