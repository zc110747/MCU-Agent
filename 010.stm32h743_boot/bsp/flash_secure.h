/**
  ******************************************************************************
  * @file    bsp/flash_secure.h
  * @brief   Shared firmware-upgrade HMAC-SHA256 secret key.
  *
  * ALL firmware images (bootloader, app, tools) share this single key.
  * The PC-side generators/verifiers use the identical byte string
  * (tools/boot_secret.py). Do NOT change it without regenerating every
  * existing upgrade package.
  ******************************************************************************
  */
#ifndef __BSP_FLASH_SECURE_H
#define __BSP_FLASH_SECURE_H

#include <stdint.h>

#define BOOT_HMAC_KEY_LEN 25U

static const uint8_t BOOT_HMAC_KEY[BOOT_HMAC_KEY_LEN] = {
    'S', 'T', 'M', '3', '2', 'H', '7', 'B', 'o', 'o', 't', 'K',
    'e', 'y', '2', '0', '2', '6', '#', 'U', '-', 'D', 'i', 's', 'k'
};

#endif /* __BSP_FLASH_SECURE_H */
