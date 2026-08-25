/**
  ******************************************************************************
  * @file    app/fs_init.c
  * @brief   QSPI FatFs one-shot preparation, run before USB MSC is enabled.
  *
  * The U-disk is exposed raw to the host. To make it usable without forcing the
  * user to format on first plug, we pre-format the QSPI with a FAT volume when
  * it is blank. This runs entirely before BSP_USB_Init(), so the host never
  * sees a half-formatted device.
  ******************************************************************************
  */
#include "fs_init.h"
#include "uart.h"
#include "ff.h"

/* Work buffer for f_mkfs (must be >= FF_MAX_SS; 4 KB is comfortable). */
#define MKFS_WORK_BUF  (4096U)
static uint8_t g_mkfs_work[MKFS_WORK_BUF];
static FATFS   g_fatfs;

int FS_PrepareForMassStorage(void)
{
    FRESULT fr;

    /* Try to mount the existing volume. Empty/blank flash -> FR_NO_FILESYSTEM. */
    fr = f_mount(&g_fatfs, "", 1);
    if (fr == FR_OK) {
        BSP_UART_Printf(" QSPI FAT: existing volume found, mount OK\r\n");
        /* Already formatted - just unmount so the host owns it cleanly. */
        f_mount(NULL, "", 0);
        BSP_UART_Printf(" QSPI FAT: unmounted (host may now format if needed)\r\n");
        return 0;
    }

    BSP_UART_Printf(" QSPI FAT: no filesystem (0x%02X), formatting... ", (unsigned)fr);
    MKFS_PARM opt = {0};
    opt.fmt  = FM_FAT;          /* FAT12/16/32 chosen by f_mkfs by size */
    opt.n_fat = 1;
    opt.align = 1;              /*  -sector alignment (we handle erase internally) */
    opt.au_size = 1024U;       /* 2 sectors/clusters -> ~8K clusters on 8MB (valid FAT16) */

    fr = f_mkfs("", &opt, g_mkfs_work, sizeof(g_mkfs_work));
    if (fr != FR_OK) {
        BSP_UART_Printf("FAIL (0x%02X)\r\n", (unsigned)fr);
        return -1;
    }
    BSP_UART_Printf("OK\r\n");
    f_mount(NULL, "", 0);   /* unmount after format */
    BSP_UART_Printf(" QSPI FAT: formatted + unmounted; host sees a clean U-disk\r\n");
    return 0;
}
