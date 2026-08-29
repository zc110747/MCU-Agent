/**
  ******************************************************************************
  * @file    bsp/bsp_wm8978.h
  * @brief   WM8978 stereo audio CODEC control over I2C2 (PH4/PH5).
  *
  *  The WM8978 is wired on the shared I2C2 bus (PH4=SCL, PH5=SDA) of the
  *  STM32F429 Apollo board.  This driver only programs the control registers
  *  (volume / routing / interface format); the audio data path itself is the
  *  SAI1 block A master link (see bsp_sai_audio.c).
  *
  *  WM8978 runs as an I2S *slave*: the STM32 SAI provides BCLK + LRCLK (FS)
  *  and the master clock (MCLK) is supplied by SAI1_MCK_A (PE2) -- see the
  *  clocking note in bsp_sai_audio.c.  No PLL is used inside the CODEC.
  *
  *  NOTE (hardware verification): the register sequence below follows the
  *  standard WM8978 "DAC -> headphone (OUT1) + speaker (OUT2)" slave-I2S path
  *  and the widely used 正点原子 reference init.  Volume / routing bit values
  *  SHOULD be confirmed on the real board (a wrong routing bit = silent
  *  output, not a crash).  The structure (I2C register write, volume mapping,
  *  interface format) is correct; the exact analog-mixer bits are the item to
  *  re-check against the WM8978 datasheet if a channel is silent.
  ******************************************************************************
  */
#ifndef __BSP_WM8978_H
#define __BSP_WM8978_H

#include <stdint.h>

/* 7-bit I2C address of the WM8978; HAL wants it pre-shifted by 1. */
#define WM8978_I2C_ADDR        (0x1AU << 1U)   /* 0x34 write, 0x35 read */

/* Volume range exposed to the UI (0..100). */
#define WM8978_VOL_MIN         0U
#define WM8978_VOL_MAX         100U

/**
  * @brief  Program the WM8978 for I2S slave playback and power up the outputs.
  *         Safe to call before the FreeRTOS scheduler is up (the I2C lock is a
  *         no-op then); uses the shared I2C2 handle from bsp_i2c.
  * @retval 0 success, -1 on I2C failure
  */
int wm8978_init(void);

/**
  * @brief  Set the playback volume (0..100). 0 = digital mute.
  */
int wm8978_set_volume(uint8_t vol);

/**
  * @brief  Last volume value set (0..100).
  */
uint8_t wm8978_get_volume(void);

#endif /* __BSP_WM8978_H */
