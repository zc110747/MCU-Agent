/**
  ******************************************************************************
  * @file    app/fs_init.c
  * @brief   QSPI FatFs volume lifecycle for the bootloader.
  *
  * On boot the bootloader mounts the FAT volume, processes any upgrade
  * package (see upgrade.c), then unmounts so the TinyUSB MSC stack can expose
  * the raw QSPI to the host. A blank flash is formatted before first use.
  ******************************************************************************
  */
#include "fs_init.h"
#include "uart.h"
#include "ff.h"

/* Work buffer for f_mkfs (must be >= FF_MAX_SS; 4 KB is comfortable). */
#define MKFS_WORK_BUF  (4096U)
static uint8_t g_mkfs_work[MKFS_WORK_BUF];
static FATFS   g_fatfs;

int FS_Mount(void)
{
    FRESULT fr;

    fr = f_mount(&g_fatfs, "", 1);
    if (fr == FR_OK) {
        BSP_UART_Printf("[FS ] FAT volume mounted\r\n");
        return 0;
    }

    /* Blank / corrupt flash: format it so the host sees a usable U-disk. */
    BSP_UART_Printf("[FS ] no filesystem (0x%02X), formatting... ", (unsigned)fr);
    MKFS_PARM opt = {0};
    opt.fmt     = FM_FAT;            /* FAT12/16/32 chosen by f_mkfs by size */
    opt.n_fat   = 1;
    opt.align   = 1;
    opt.au_size = 1024U;

    fr = f_mkfs("", &opt, g_mkfs_work, sizeof(g_mkfs_work));
    if (fr != FR_OK) {
        BSP_UART_Printf("FAIL (0x%02X)\r\n", (unsigned)fr);
        return -1;
    }
    BSP_UART_Printf("OK\r\n");

    fr = f_mount(&g_fatfs, "", 1);
    if (fr != FR_OK) {
        BSP_UART_Printf("[FS ] remount FAIL (0x%02X)\r\n", (unsigned)fr);
        return -1;
    }
    BSP_UART_Printf("[FS ] formatted, volume mounted\r\n");
    return 0;
}

void FS_Unmount(void)
{
    f_mount(NULL, "", 0);
    BSP_UART_Printf("[FS ] unmounted (USB MSC takes over)\r\n");
}
