/*-----------------------------------------------------------------------*/
/* FatFs disk interface glue for the 010.stm32h743_boot project.          */
/*                                                                       */
/* The generic diskio.c forwards physical drive 0 (DEV_RAM) to the        */
/* RAM_disk_* functions below, which are mapped onto the QSPI-backed      */
/* block driver in fs_qspi.c. MMC/USB are kept as no-op stubs because     */
/* this bootloader only exposes the internal QSPI flash as a FAT volume.  */
/*-----------------------------------------------------------------------*/
#ifndef DISK_INTERFACE_H
#define DISK_INTERFACE_H

#include "ff.h"
#include "diskio.h"

/* QSPI flash block driver (implemented in fs_qspi.c). */
DSTATUS qspi_disk_initialize(void);
DSTATUS qspi_disk_status(void);
DRESULT qspi_disk_read(BYTE *buff, LBA_t sector, UINT count);
DRESULT qspi_disk_write(const BYTE *buff, LBA_t sector, UINT count);
DRESULT qspi_disk_ioctl(BYTE cmd, void *buff);

/* RAM disk = physical drive 0 (DEV_RAM) -> QSPI flash */
int RAM_disk_status(void);
int RAM_disk_initialize(void);
int RAM_disk_read(BYTE *buff, LBA_t sector, UINT count);
int RAM_disk_write(const BYTE *buff, LBA_t sector, UINT count);
int RAM_disk_ioctl(BYTE cmd, void *buff);

/* MMC/SD (unused in this project) */
int MMC_disk_status(void);
int MMC_disk_initialize(void);
int MMC_disk_read(BYTE *buff, LBA_t sector, UINT count);
int MMC_disk_write(const BYTE *buff, LBA_t sector, UINT count);
int MMC_disk_ioctl(BYTE cmd, void *buff);

/* USB MSD (unused in this project) */
int USB_disk_status(void);
int USB_disk_initialize(void);
int USB_disk_read(BYTE *buff, LBA_t sector, UINT count);
int USB_disk_write(const BYTE *buff, LBA_t sector, UINT count);

DWORD get_fattime(void);

#endif /* DISK_INTERFACE_H */
