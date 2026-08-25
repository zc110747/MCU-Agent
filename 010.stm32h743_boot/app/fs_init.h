/**
  ******************************************************************************
  * @file    app/fs_init.h
  * @brief   QSPI FAT volume lifecycle (mount / format-blank / unmount).
  ******************************************************************************
  */
#ifndef __FS_INIT_H
#define __FS_INIT_H

/* Mount the FAT volume on QSPI; if the flash is blank, format it first.
   Returns 0 on success, -1 on failure. */
int FS_Mount(void);

/* Unmount so the USB MSC callbacks can own the raw QSPI device. */
void FS_Unmount(void);

#endif /* __FS_INIT_H */
