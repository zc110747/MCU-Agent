/* ---------------------------------------------------------------------------
 * FatFs low-level block driver for the QSPI flash (HAL indirect mode).
 *
 * FatFs talks to the storage through the generic diskio glue in diskio.c, which
 * forwards everything for physical drive 0 to these functions. The QSPI driver
 * already does read-modify-erase-program internally, so the FAT layer can treat
 * the 8 MB device as a 512-byte-sector disk.
 * -------------------------------------------------------------------------*/
#include "ff.h"
#include "diskio.h"
#include "qspi.h"

#define QSPI_FS_SECTORS  ((uint32_t)(QSPI_FLASH_SIZE / 512U))

/* FatFs calls this once per mounted volume. Initialise the QSPI peripheral in
   HAL indirect mode (idempotent - the boot code already did this, but FatFs may
   be the first to touch it if mounted lazily). */
DSTATUS qspi_disk_initialize(void)
{
    if (BSP_QSPI_Init() != QSPI_OK)
        return STA_NOINIT;
    return RES_OK;
}

DSTATUS qspi_disk_status(void)
{
    if (BSP_QSPI_IsBusy())
        return STA_NOINIT;
    return RES_OK;
}

DRESULT qspi_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    uint32_t addr = (uint32_t)sector * 512U;
    uint32_t len  = (uint32_t)count * 512U;
    if (BSP_QSPI_ReadIndirect(addr, buff, len) != QSPI_OK)
        return RES_ERROR;
    return RES_OK;
}

DRESULT qspi_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    uint32_t addr = (uint32_t)sector * 512U;
    uint32_t len  = (uint32_t)count * 512U;

    /* The flash cannot be written in place. Read-modify-erase-program at 4 KB
       sector granularity (one or more depending on span). Static scratch keeps
       it simple and allocation-free. */
    static uint8_t scratch[4096];
    uint32_t pos = addr;
    uint32_t remaining = len;
    const uint8_t *src = buff;

    while (remaining > 0) {
        uint32_t sec_addr   = pos & ~0xFFFU;          /* 4 KB aligned */
        uint32_t in_off     = pos - sec_addr;
        uint32_t chunk      = remaining;
        if (in_off + chunk > 4096U) chunk = 4096U - in_off;

        if (BSP_QSPI_ReadIndirect(sec_addr, scratch, 4096U) != QSPI_OK)
            return RES_ERROR;
        for (uint32_t i = 0; i < chunk; i++) scratch[in_off + i] = src[i];

        if (BSP_QSPI_EraseSector(sec_addr) != QSPI_OK)
            return RES_ERROR;
        for (uint32_t pg = 0; pg < 4096U; pg += QSPI_PAGE_SIZE) {
            if (BSP_QSPI_WritePage(sec_addr + pg, &scratch[pg], QSPI_PAGE_SIZE) != QSPI_OK)
                return RES_ERROR;
        }

        pos       += chunk;
        src       += chunk;
        remaining -= chunk;
    }
    return RES_OK;
}

DRESULT qspi_disk_ioctl(BYTE cmd, void *buff)
{
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = (LBA_t)QSPI_FS_SECTORS;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = (DWORD)(QSPI_SECTOR_SIZE / 512U);  /* erase unit in sectors */
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
