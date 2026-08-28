/*-----------------------------------------------------------------------------
 * exFAT genuineness + 128KB alignment verification harness (PC / host build).
 *
 * Compiles the SAME FatFs R0.15 (third_party/FatFs) and the SAME ffconf.h
 * (FF_FS_EXFAT=1) used by the firmware, but against a RAM disk instead of a
 * USB MSC device.  It runs f_mkfs() with the firmware's exact MKFS_PARM and
 * then inspects the raw volume to prove:
 *   (1) the volume is genuinely exFAT (boot signature "EXFAT   " + fs_type),
 *   (2) the DATA REGION (cluster heap) is aligned to 128 KB,
 *   (3) the allocation unit (cluster) is exactly 128 KB.
 *
 * This is the honest substitute for hardware: it exercises the exact code
 * path the firmware uses, so a PASS here means the on-target f_mkfs would
 * emit a real, 128KB-aligned exFAT volume.
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "ff.h"
#include "diskio.h"

/* ---- RAM disk (64 MB) ------------------------------------------------ */
#define SECTOR_SIZE   512U
#define DISK_SECTORS  (131072U)                       /* 64 MB */
static uint8_t g_disk[DISK_SECTORS * SECTOR_SIZE];

/* ---- diskio glue for the RAM disk ----------------------------------- */
static DSTATUS ram_status(BYTE pdrv)        { (void)pdrv; return 0; }
static DSTATUS ram_initialize(BYTE pdrv)    { (void)pdrv; return 0; }
static DRESULT ram_read(BYTE pdrv, BYTE *buf, LBA_t sec, UINT count)
{
  (void)pdrv;
  memcpy(buf, &g_disk[sec * SECTOR_SIZE], (size_t)count * SECTOR_SIZE);
  return RES_OK;
}
static DRESULT ram_write(BYTE pdrv, const BYTE *buf, LBA_t sec, UINT count)
{
  (void)pdrv;
  memcpy(&g_disk[sec * SECTOR_SIZE], buf, (size_t)count * SECTOR_SIZE);
  return RES_OK;
}
static DRESULT ram_ioctl(BYTE pdrv, BYTE cmd, void *buf)
{
  (void)pdrv;
  switch (cmd)
  {
    case GET_SECTOR_COUNT: *(DWORD*)buf = DISK_SECTORS; return RES_OK;
    case GET_SECTOR_SIZE:  *(WORD*)buf  = SECTOR_SIZE;  return RES_OK;
    case GET_BLOCK_SIZE:   *(DWORD*)buf = 1;            return RES_OK;
    case CTRL_SYNC:        return RES_OK;
    default:               return RES_PARERR;
  }
}

DSTATUS disk_status(BYTE pdrv)     { return ram_status(pdrv); }
DSTATUS disk_initialize(BYTE pdrv) { return ram_initialize(pdrv); }
DRESULT disk_read(BYTE pdrv, BYTE *buf, LBA_t sec, UINT cnt)  { return ram_read(pdrv, buf, sec, cnt); }
DRESULT disk_write(BYTE pdrv, const BYTE *buf, LBA_t sec, UINT cnt) { return ram_write(pdrv, buf, sec, cnt); }
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buf) { return ram_ioctl(pdrv, cmd, buf); }

/* ---- little-endian reader on the raw disk image -------------------- */
static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- test counters ------------------------------------------------- */
static int g_pass = 0;
static int g_fail = 0;
#define CHECK(cond, ...) do { \
  if (cond) { g_pass++; printf("  [PASS] "); printf(__VA_ARGS__); printf("\n"); } \
  else      { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

int main(void)
{
  FATFS  fatfs;
  FIL    file;
  FRESULT rc;
  UINT   bw, br;
  BYTE   work[16384];
  static const char *text =
      "STM32F429 USB Host FATFS Test\r\n"
      "Hello from USB Flash Disk!\r\n"
      "FreeRTOS + USB Host + exFAT\r\n";
  char   rbuf[128];

  printf("=== exFAT verification harness (FatFs R0.15, FF_FS_EXFAT=%d) ===\n",
         FF_FS_EXFAT);

  /* ---- (A) format with the firmware's exact parameters ---- */
  MKFS_PARM opt;
  memset(&opt, 0, sizeof(opt));
  opt.fmt     = FM_EXFAT;                 /* real exFAT */
  opt.n_fat   = 1;
  opt.align   = (128u * 1024u) / 512u;    /* 256 sectors = 128 KB alignment */
  opt.au_size = 128u * 1024u;             /* 128 KB allocation unit */
  opt.n_root  = 0;

  printf("\n[A] f_mkfs(FM_EXFAT, align=%u sectors, au_size=%u bytes)\n",
         (unsigned)opt.align, (unsigned)opt.au_size);
  rc = f_mkfs("0:", &opt, work, sizeof(work));
  CHECK(rc == FR_OK, "f_mkfs() returned FR_OK");

  /* ---- (B) mount and read back fs_type ---- */
  printf("\n[B] f_mount()\n");
  rc = f_mount(&fatfs, "0:", 1);
  CHECK(rc == FR_OK, "f_mount() returned FR_OK");
#if FF_FS_EXFAT
  CHECK(fatfs.fs_type == 4, "mounted fs_type == FS_EXFAT (4) -- not FAT32");
#else
  CHECK(0, "FF_FS_EXFAT is 0 in this build (misconfigured)");
#endif

  /* ---- (C) write + read back test.txt (round trip) ---- */
  printf("\n[C] write/read test.txt round trip\n");
  rc = f_open(&file, "0:test.txt", FA_CREATE_ALWAYS | FA_WRITE);
  CHECK(rc == FR_OK, "f_open(create) OK");
  if (rc == FR_OK)
  {
    rc = f_write(&file, text, (UINT)strlen(text), &bw);
    CHECK(rc == FR_OK && bw == strlen(text), "f_write wrote full text");
    f_close(&file);

    rc = f_open(&file, "0:test.txt", FA_READ);
    CHECK(rc == FR_OK, "f_open(read) OK");
    if (rc == FR_OK)
    {
      memset(rbuf, 0, sizeof(rbuf));
      rc = f_read(&file, rbuf, sizeof(rbuf) - 1, &br);
      CHECK(rc == FR_OK && br == strlen(text) &&
            memcmp(rbuf, text, strlen(text)) == 0,
            "f_read returned identical content");
      f_close(&file);
    }
  }

  /* ---- (D) inspect RAW volume: genuine exFAT + 128KB data-aligned ---- */
  printf("\n[D] raw volume inspection\n");

  /* Locate the Main Boot Sector (VBR) by scanning for the "EXFAT   " name. */
  int boot_lba = -1;
  for (int s = 0; s < (int)DISK_SECTORS; s++)
  {
    const uint8_t *cand = &g_disk[s * SECTOR_SIZE];
    if (memcmp(&cand[3], "EXFAT", 5) == 0) { boot_lba = s; break; }
  }
  CHECK(boot_lba >= 0, "found \"EXFAT   \" boot signature in image");
  if (boot_lba < 0) goto summary;

  const uint8_t *boot = &g_disk[boot_lba * SECTOR_SIZE];
  uint8_t  bps_shift = boot[0x6C];        /* bytes per sector shift */
  uint8_t  spc_shift = boot[0x6D];        /* sectors per cluster shift */
  uint32_t fat_off   = rd_u32(&boot[0x50]);  /* FAT offset (rel. to VBR) */
  uint32_t heap_off  = rd_u32(&boot[0x58]);  /* cluster-heap offset (rel. to VBR) */

  uint32_t abs_fat  = (uint32_t)boot_lba + fat_off;    /* absolute LBA */
  uint32_t abs_heap = (uint32_t)boot_lba + heap_off;   /* absolute LBA */
  uint32_t sector_sz = 1u << bps_shift;
  uint32_t au_bytes  = (1u << spc_shift) * sector_sz;   /* allocation unit */

  printf("      VBR at LBA %d; bytes/sector=%u; sectors/cluster=%u\n",
         boot_lba, sector_sz, 1u << spc_shift);
  printf("      FAT  abs LBA=%u (0x%X);  ClusterHeap abs LBA=%u (0x%X)\n",
         abs_fat, abs_fat, abs_heap, abs_heap);
  printf("      allocation unit = %u KB\n", au_bytes / 1024u);

  uint32_t align_sectors = opt.align;     /* 256 = 128 KB */
  CHECK(memcmp(&boot[3], "EXFAT", 5) == 0,
        "boot signature == \"EXFAT\" (genuine exFAT, not FAT32)");
  CHECK(bps_shift == 9, "sector size is 512 B");
  CHECK(au_bytes == 128u * 1024u,
        "allocation unit (cluster) == 128 KB (got %u KB)", au_bytes / 1024u);
  CHECK((abs_heap % align_sectors) == 0,
        "DATA REGION (cluster heap) aligned to 128 KB (LBA %u %% 256 == 0)",
        abs_heap);
  printf("      [info] FAT region abs LBA=%u (exFAT aligns the data region, "
         "not the FAT, per spec)\n", abs_fat);

summary:
  /* ---- summary ---- */
  printf("\n========================================\n");
  printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
  if (g_fail == 0)
    printf("VERDICT: PASS -- genuine exFAT, 128KB-aligned data region, 128KB AU\n");
  else
    printf("VERDICT: FAIL\n");
  printf("========================================\n");

  return (g_fail == 0) ? 0 : 1;
}
