/**
  ******************************************************************************
  * @file    app/boot.h
  * @brief   Boot decision state machine entry.
  ******************************************************************************
  */
#ifndef __BOOT_H
#define __BOOT_H

/* Read system config, validate the app region, then either count down to a
   jump (8 s, USB-disconnected) or stay in U-disk mode (USB connected or the
   app image is invalid). Never returns on the jump path. */
void BSP_Boot_Enter(void);

#endif /* __BOOT_H */
