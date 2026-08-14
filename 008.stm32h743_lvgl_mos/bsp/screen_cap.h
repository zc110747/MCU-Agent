/**
  ******************************************************************************
  * @file    screen_cap.h
  * @brief   Capture the current OLED framebuffer to a JPEG on the SD card.
  ******************************************************************************
  */
#ifndef __SCREEN_CAP_H
#define __SCREEN_CAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief  Capture destination directory on the SD card (created on first use). */
#define CAP_DIR  "1:/catch"

/**
  * @brief  Capture the current OLED content as a JPEG under CAP_DIR.
  *
  * File name format:  HH-MM-SS-NNN.jpg  (NNN = 3-digit random nonce).
  * If the name already exists a "_k" suffix is appended to avoid overwriting.
  *
  * @param[out] out_path  Receives the saved path (CAP_DIR"/..."); may be NULL.
  * @param[in]  path_size Size of @p out_path in bytes.
  * @retval  0  success
  * @retval <0  failure (see screen_cap.c for the per-code meaning)
  */
int screen_cap_capture(char *out_path, int path_size);

#ifdef __cplusplus
}
#endif

#endif /* __SCREEN_CAP_H */
