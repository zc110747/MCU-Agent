/**
  ******************************************************************************
  * @file    test_app/app/app_version.c
  * @brief   Fixed 4-byte version slot at 0x08021000.
  *
  * Each component is 0..99. The bootloader reads this slot to validate the
  * image and compares it against the version stored in the system config
  * sector. Keep this in sync with the verify.json "version" used to build the
  * upgrade package (see tools/gen_upgrade.py, which reads it automatically).
  ******************************************************************************
  */
#include <stdint.h>

__attribute__((section(".app_version"), used))
const uint8_t app_version[4] = { 1, 0, 0, 6 };   /* v1.0.0.6 (upgrade-target build) */
