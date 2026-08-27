/*-----------------------------------------------------------------------*/
/* FatFs disk interface glue for the 010.stm32h743_boot project.          */
/*                                                                       */
/* Physical drive 0 (DEV_RAM) is the QSPI flash; the RAM_disk_* entry     */
/* points simply delegate to the qspi_disk_* block driver in fs_qspi.c.   */
/* MMC and USB drives are unused and return success so FatFs stays happy. */
/*-----------------------------------------------------------------------*/
#include "disk_interface.h"

/* ---- Physical drive 0: QSPI flash (mapped as the "RAM" drive) -------- */
int RAM_disk_status(void)
{
    return (int)qspi_disk_status();
}

int RAM_disk_initialize(void)
{
    return (int)qspi_disk_initialize();
}

int RAM_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    return (int)qspi_disk_read(buff, sector, count);
}

int RAM_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    return (int)qspi_disk_write(buff, sector, count);
}

int RAM_disk_ioctl(BYTE cmd, void *buff)
{
    return (int)qspi_disk_ioctl(cmd, buff);
}

/* ---- MMC/SD (unused) ------------------------------------------------- */
int MMC_disk_status(void)     { return 0; }
int MMC_disk_initialize(void)  { return 0; }
int MMC_disk_read(BYTE *buff, LBA_t sector, UINT count) { (void)buff; (void)sector; (void)count; return 0; }
int MMC_disk_write(const BYTE *buff, LBA_t sector, UINT count) { (void)buff; (void)sector; (void)count; return 0; }
int MMC_disk_ioctl(BYTE cmd, void *buff) { (void)cmd; (void)buff; return 0; }

/* ---- USB MSD (unused) ------------------------------------------------ */
int USB_disk_status(void)     { return 0; }
int USB_disk_initialize(void)  { return 0; }
int USB_disk_read(BYTE *buff, LBA_t sector, UINT count) { (void)buff; (void)sector; (void)count; return 0; }
int USB_disk_write(const BYTE *buff, LBA_t sector, UINT count) { (void)buff; (void)sector; (void)count; return 0; }

DWORD get_fattime(void)
{
    return 0;
}
