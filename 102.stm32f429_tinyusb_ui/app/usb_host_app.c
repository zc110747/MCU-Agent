/**
  ******************************************************************************
  * @file    usb_host_app.c
  * @brief   USB FS Host (TinyUSB) -> MSC -> SCSI -> FatFs (exFAT) integration.
  *
  *   USB Host Task (tuh_task)
 *        -> tuh_msc_mount_cb (device attached)
 *        -> FatFs diskio glue (disk_read/write/ioctl via tuh_msc_*)
 *        -> file_task: f_mount -> list + dump every file on the U disk
 *           (optional demo seed) -> USART print
 *
  *   The diskio glue mirrors TinyUSB's reference msc_file_explorer example:
  *   disk_read/write submit a SCSI READ10/WRITE10 and BLOCK until the
  *   completion callback (fired inside tuh_task) clears the busy flag.  This
  *   requires tuh_task() (usbh_host_task) and file_task to run concurrently,
  *   which is exactly the two-task design below.
  ******************************************************************************
  */
#include <string.h>
#include <stdio.h>

#include "tusb.h"
#include "ff.h"
#include "diskio.h"

#include "bsp_led.h"
#include "bsp_uart.h"
#include "bsp_usb_hw.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "usb_host_app.h"

/* Defined in main.c; used here so a tusb_init() failure can trap safely. */
extern void Error_Handler(void);

/* ------------------------------------------------------------------ */
/* Build-time policy                                                    */
/* ------------------------------------------------------------------ */
/* NEVER auto-format on every insertion: that would destroy user data.
 * Set to 1 only for a one-off factory/test formatting pass. */
#ifndef USB_DISK_AUTO_FORMAT
#define USB_DISK_AUTO_FORMAT   0
#endif

/* exFAT alignment / allocation unit:
 *   - Allocation unit (cluster) = 128 KB (AU_SIZE)
 *   - FAT / data region alignment = 128 KB  (align = 128KB / 512B = 256 sectors)
 * Both are honored by f_mkfs() below.  The PC-side verify harness proves
 * the geometry is genuinely 128 KB aligned (not faked). */
#define EXFAT_AU_SIZE_BYTES    ((uint32_t)(128u * 1024u))
#define EXFAT_ALIGN_SECTORS    (EXFAT_AU_SIZE_BYTES / 512u)

/* ------------------------------------------------------------------ */
/* Shared state                                                        */
/* ------------------------------------------------------------------ */
volatile usb_state_t g_usb_state = USB_DISCONNECTED;

static FATFS        fatfs[1];

static volatile bool _disk_busy[1];
static SemaphoreHandle_t xUsbMountSem = NULL;

/* Serializes ALL FatFs access (file_task explore + ui_task font load/render)
 * so two tasks never issue MSC reads/writes on the same LUN concurrently.
 * The MSC diskio glue uses a single shared _disk_busy flag; concurrent access
 * from two tasks corrupts it (lost wakeup -> permanent spin).  This mutex is
 * taken around every FatFs entry point. */
static SemaphoreHandle_t xFsLock = NULL;

void fs_lock(void)
{
    if (xFsLock != NULL)
    {
        (void)xSemaphoreTake(xFsLock, portMAX_DELAY);
    }
}

void fs_unlock(void)
{
    if (xFsLock != NULL)
    {
        (void)xSemaphoreGive(xFsLock);
    }
}

/* ------------------------------------------------------------------ */
/* FatFs <-> USB MSC diskio glue                                       */
/* ------------------------------------------------------------------ */
static void wait_for_disk_io(BYTE pdrv)
{
  while (_disk_busy[pdrv]) vTaskDelay(1);
}

static bool disk_io_complete(uint8_t dev_addr, const tuh_msc_complete_data_t *cb_data)
{
  (void)dev_addr; (void)cb_data;
  _disk_busy[0] = false;
  return true;
}

DSTATUS disk_status(BYTE pdrv)
{
  (void)pdrv;
  return tuh_msc_mounted((uint8_t)(pdrv + 1)) ? 0 : STA_NODISK;
}

DSTATUS disk_initialize(BYTE pdrv)
{
  (void)pdrv;
  return 0;   /* nothing to do; TinyUSB owns the transport */
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
  const uint8_t dev_addr = (uint8_t)(pdrv + 1);
  _disk_busy[pdrv] = true;
  tuh_msc_read10(dev_addr, 0, buff, sector, (uint16_t)count, disk_io_complete, 0);
  wait_for_disk_io(pdrv);
  return RES_OK;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
  const uint8_t dev_addr = (uint8_t)(pdrv + 1);
  _disk_busy[pdrv] = true;
  tuh_msc_write10(dev_addr, 0, buff, sector, (uint16_t)count, disk_io_complete, 0);
  wait_for_disk_io(pdrv);
  return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  const uint8_t dev_addr = (uint8_t)(pdrv + 1);
  switch (cmd)
  {
    case CTRL_SYNC:
      return RES_OK;
    case GET_SECTOR_COUNT:
      *((DWORD *)buff) = (DWORD)tuh_msc_get_block_count(dev_addr, 0);
      return RES_OK;
    case GET_SECTOR_SIZE:
      *((WORD *)buff) = (WORD)tuh_msc_get_block_size(dev_addr, 0);
      return RES_OK;
    case GET_BLOCK_SIZE:
      *((DWORD *)buff) = 1;
      return RES_OK;
    default:
      return RES_PARERR;
  }
}

/* ------------------------------------------------------------------ */
/* exFAT formatting (guarded; default OFF)                             */
/* ------------------------------------------------------------------ */
static FRESULT format_exfat(void)
{
  MKFS_PARM opt;
  static uint8_t work[16384];   /* f_mkfs scratch buffer (>= sector size) */

  memset(&opt, 0, sizeof(opt));
  opt.fmt      = FM_EXFAT;            /* real exFAT, not FAT32 */
  opt.n_fat    = 1;
  opt.align    = EXFAT_ALIGN_SECTORS; /* 128 KB aligned FAT / data region */
  opt.au_size  = EXFAT_AU_SIZE_BYTES; /* 128 KB allocation unit (cluster) */
  opt.n_root   = 0;

  printf("Formatting exFAT (AU=128KB, align=128KB)...\r\n");
  return f_mkfs("0:", &opt, work, sizeof(work));
}

/* ------------------------------------------------------------------ */
/* Demo seed: write a few deterministic files (incl. a sub-directory)  */
/* into 0:/demo/ so the read/print path can be verified on real HW     */
/* without depending on pre-existing user files.                       */
/* ------------------------------------------------------------------ */
#ifndef USB_DISK_SEED_DEMO
#define USB_DISK_SEED_DEMO   1   /* set 0 for pure read of user's own disk */
#endif

static void seed_demo_files(void)
{
  f_mkdir("0:/demo");
  f_mkdir("0:/demo/sub");

  const struct { const char *path; const char *body; } seeds[] =
  {
    { "0:/demo/hello.txt",  "Hello from STM32F429 USB Host\r\n" },
    { "0:/demo/notes.txt",  "Line A\r\nLine B\r\nLine C\r\n" },
    { "0:/demo/sub/world.txt", "Nested directory file content\r\n" },
  };
  for (unsigned i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++)
  {
    FIL f;
    FRESULT rc = f_open(&f, seeds[i].path, FA_CREATE_ALWAYS | FA_WRITE);
    if (rc != FR_OK)
    {
      printf("seed open '%s' failed rc=%d\r\n", seeds[i].path, rc);
      continue;
    }
    UINT bw;
    f_write(&f, seeds[i].body, (UINT)strlen(seeds[i].body), &bw);
    f_close(&f);
  }
}

/* ------------------------------------------------------------------ */
/* File content dump: emit raw bytes over serial, sanitizing           */
/* non-printables to '.' so binary files stay terminal-friendly.       */
/* ------------------------------------------------------------------ */
#define DISK_MAX_DEPTH       5
#define DISK_MAX_DUMP_BYTES  2048  /* cap per-file dump to avoid flooding */

static void dump_chunk(const char *buf, UINT len)
{
  char out[64];
  UINT j = 0;
  for (UINT i = 0; i < len; i++)
  {
    char c = buf[i];
    if (c < 0x20 && c != '\r' && c != '\n' && c != '\t') c = '.';
    out[j++] = c;
    if (j == sizeof(out))
    {
      uart_write((const uint8_t *)out, (int)j);  /* explicit len: out[] has no NUL */
      j = 0;
    }
  }
  if (j) uart_write((const uint8_t *)out, (int)j);
}

static void dump_file(const char *path, uint32_t fsize)
{
  FIL fil;
  FRESULT rc = f_open(&fil, path, FA_READ);
  if (rc != FR_OK)
  {
    printf("  [open failed rc=%d]\r\n", rc);
    return;
  }
  printf("  === content (%lu bytes) ===\r\n", (unsigned long)fsize);
  char buf[64];
  UINT br;
  uint32_t total = 0;
  bool truncated = false;
  while ((rc = f_read(&fil, buf, sizeof(buf), &br)) == FR_OK && br > 0)
  {
    if (total + br > DISK_MAX_DUMP_BYTES)
    {
      UINT allow = (UINT)(DISK_MAX_DUMP_BYTES - total);
      dump_chunk(buf, allow);
      total = DISK_MAX_DUMP_BYTES;
      truncated = true;
      break;
    }
    dump_chunk(buf, br);
    total += br;
  }
  if (rc != FR_OK) printf("\r\n  [read error rc=%d]\r\n", rc);
  f_close(&fil);
  if (truncated) printf("\r\n  [truncated at %u bytes]\r\n", DISK_MAX_DUMP_BYTES);
  printf("  === end ===\r\n");
}

/* ------------------------------------------------------------------ */
/* Recursive directory traversal: list + dump every file              */
/* ------------------------------------------------------------------ */
static void explore_dir(const char *dir_path, int depth,
                        uint32_t *pfiles, uint32_t *pdirs)
{
  if (depth > DISK_MAX_DEPTH) return;

  DIR dir;
  FRESULT rc = f_opendir(&dir, dir_path);
  if (rc != FR_OK)
  {
    printf("f_opendir('%s') failed rc=%d\r\n", dir_path, rc);
    return;
  }

  FILINFO fno;
  for (;;)
  {
    rc = f_readdir(&dir, &fno);
    if (rc != FR_OK)
    {
      printf("f_readdir failed rc=%d\r\n", rc);
      break;
    }
    if (fno.fname[0] == 0) break;  /* end of directory */

    const char *name = fno.fname;  /* LFN buffer in this FatFs build */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

    char full[320];
    size_t plen = strlen(dir_path);
    (void)snprintf(full, sizeof(full), "%s%s%s", dir_path,
                   (plen > 0 && dir_path[plen - 1] == '/') ? "" : "/",
                   name);

    bool is_dir = (fno.fattrib & AM_DIR) != 0;
    if (is_dir)
    {
      (*pdirs)++;
      printf("[DIR ] %s\r\n", full);
      explore_dir(full, depth + 1, pfiles, pdirs);
    }
    else
    {
      (*pfiles)++;
      printf("[FILE] %s  (%lu bytes)\r\n", full, (unsigned long)fno.fsize);
      dump_file(full, fno.fsize);
    }
  }
  f_closedir(&dir);
}

/* ------------------------------------------------------------------ */
/* Main action after mount: seed (optional) + list + dump + heap proof  */
/* ------------------------------------------------------------------ */
static void usb_disk_explore(void)
{
  uint32_t nfiles = 0, ndirs = 0;

  printf("USB Disk Mounted\r\n");

#if USB_DISK_SEED_DEMO
  seed_demo_files();
#endif

  printf("========== USB DISK CONTENTS ==========\r\n");
  explore_dir("0:/", 0, &nfiles, &ndirs);
  printf("========== END (dirs=%lu files=%lu) ==========\r\n",
         (unsigned long)ndirs, (unsigned long)nfiles);

  /* Print the address of a heap object to prove the FreeRTOS heap lives in
   * external SDRAM (0xC0000000). */
  void *p = pvPortMalloc(32);
  if (p)
  {
    printf("Heap object @ 0x%08X (SDRAM base 0xC0000000)\r\n", (unsigned int)(uintptr_t)p);
    vPortFree(p);
  }
}

/* ------------------------------------------------------------------ */
/* file task: waits for mount, then exercises the filesystem            */
/* ------------------------------------------------------------------ */
static void file_task(void *arg)
{
  (void)arg;
  FRESULT rc;
  char path[3] = "0:";

  for (;;)
  {
    /* Block until a mass-storage device is mounted (binary semaphore given
     * from tuh_msc_mount_cb, which runs inside usbh_host_task). */
    xSemaphoreTake(xUsbMountSem, portMAX_DELAY);

    if (!tuh_msc_mounted(1))
    {
      continue;
    }
    g_usb_state = USB_MSC_READY;

    /* Serialize all FatFs work for this LUN with the UI font task. */
    fs_lock();

    rc = f_mount(&fatfs[0], path, 1);
    if (rc != FR_OK)
    {
      if (USB_DISK_AUTO_FORMAT && rc == FR_NO_FILESYSTEM)
      {
        f_mount(NULL, path, 0);            /* unmount first */
        rc = format_exfat();
        if (rc == FR_OK)
        {
          rc = f_mount(&fatfs[0], path, 1);
        }
      }
      if (rc != FR_OK)
      {
        printf("f_mount failed: %d\r\n", rc);
        g_usb_state = USB_ERROR;
        fs_unlock();
        continue;
      }
    }

    g_usb_state = USB_MOUNTED;
    usb_disk_explore();

    fs_unlock();
  }
}

/* ------------------------------------------------------------------ */
/* TinyUSB host callbacks                                              */
/* ------------------------------------------------------------------ */
void tuh_msc_mount_cb(uint8_t dev_addr)
{
  (void)dev_addr;
  printf("USB Disk Connected (MSC ready)\r\n");
  g_usb_state = USB_ENUMERATED;
  /* Unblock the file task.  NOTE: this callback runs inside tuh_task(), which
   * executes in the usbh_host_task context (NOT an ISR), so the plain
   * xSemaphoreGive is correct here (no FromISR variant). */
  if (xUsbMountSem != NULL)
  {
    xSemaphoreGive(xUsbMountSem);
  }
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
  (void)dev_addr;
  printf("USB Disk Removed\r\n");
  g_usb_state = USB_DISCONNECTED;
  f_mount(NULL, "0:", 0);
}

/* ------------------------------------------------------------------ */
/* Init + tasks                                                        */
/* ------------------------------------------------------------------ */
bool usbh_app_init(void)
{
  _disk_busy[0] = false;
  xUsbMountSem = xSemaphoreCreateBinary();
  if (xUsbMountSem == NULL) return false;

  xFsLock = xSemaphoreCreateMutex();
  if (xFsLock == NULL) return false;

  if (xTaskCreate(file_task, "file", 2048, NULL,
                 tskIDLE_PRIORITY + 2, NULL) != pdPASS)
  {
    return false;
  }
  return true;
}

void usbh_host_task(void *arg)
{
  (void)arg;

  /* USB FS Host hardware + stack init.
   * MUST run AFTER the OS is up -- this task is created by main() and only
   * starts after vTaskStartScheduler().  tusb_init() enables the OTG FS
   * interrupt; its ISR uses FreeRTOS FromISR queue calls (xQueueSendToBack
   * FromISR / xSemaphoreGiveFromISR) that are only valid once the scheduler
   * is running.  Doing this in main() (pre-scheduler) let the interrupt fire
   * into a not-yet-live RTOS and corrupt system state. */
  USBH_HW_Init();
  tusb_rhport_init_t host_init = { .role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO };
  if (!tusb_init(0, &host_init))
  {
    printf("tusb_init FAILED\r\n");
    Error_Handler();
  }
  printf("USB Host Init\r\n");

  for (;;)
  {
    tuh_task();
  }
}
