/**
  ******************************************************************************
  * @file    app/fs_init.h
  * @brief   QSPI FatFs one-shot preparation, run before USB MSC is enabled.
  ******************************************************************************
  */
#ifndef __FS_INIT_H
#define __FS_INIT_H

#include <stdint.h>

/* Prepare the QSPI flash for the U-disk role: if no FAT volume is present,
   create one with f_mkfs, then always unmount so the host can enumerate a
   clean, writable disk. Returns 0 on success, <0 on failure. */
int FS_PrepareForMassStorage(void);

#endif /* __FS_INIT_H */
