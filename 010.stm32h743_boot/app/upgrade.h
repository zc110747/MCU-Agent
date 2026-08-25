/**
  ******************************************************************************
  * @file    app/upgrade.h
  * @brief   U-disk (QSPI FAT) firmware upgrade entry.
  ******************************************************************************
  */
#ifndef __UPGRADE_H
#define __UPGRADE_H

/* Check the QSPI FAT volume for an upgrade package (stm32h7_xx.bin +
   verify.json) and perform the upgrade if all checks pass.
   Returns: 1 = upgraded, 0 = no package / skipped, -1 = error. */
int BSP_Upgrade_Check(void);

#endif /* __UPGRADE_H */
