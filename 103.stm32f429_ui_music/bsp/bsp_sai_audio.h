/**
  ******************************************************************************
  * @file    bsp/bsp_sai_audio.h
  * @brief   SAI1 block A master-TX audio link for the WM8978 (STM32F429).
  *
  *  Wiring (all SAI1_A on GPIOE, AF6):
  *     PE2  SAI1_MCK_A   master clock  -> WM8978 MCLK
  *     PE3  SAI1_SD_B    (RX, unused)  -> WM8978 ADCDAT
  *     PE4  SAI1_FS_A    LRCLK / WS    -> WM8978 LRC
  *     PE5  SAI1_SCK_A   bit clock     -> WM8978 BCLK
  *     PE6  SAI1_SD_A    TX data       -> WM8978 DACDAT
  *
  *  The link runs as I2S master: the STM32 emits BCLK + LRCLK + MCLK, the
  *  WM8978 is an I2S slave.  Audio samples are moved by a DMA2_Stream3
  *  (channel 0) circular double-buffer living in external SDRAM; the player
  *  task refills the half that just finished playing.
  *
  *  CLOCKING NOTE (verify on hardware): SAI1CLK is sourced from PLLSAI
  *  (configured here to 48 MHz).  The CODEC MCLK is driven by SAI1_MCK_A with
  *  MCKDIV=2 -> ~12 MHz (256xFs-ish).  If the real board instead wires the
  *  CODEC MCLK to PA3 (PWM_AUDIO), switch AUDIO_MCLK_USE_PA3 on and see
  *  bsp_sai_audio.c; the SAI MCK pin then carries the same frequency.  Exact
  *  MCLK/BCLK dividers may need a small tweak against a frequency counter.
  ******************************************************************************
  */
#ifndef __BSP_SAI_AUDIO_H
#define __BSP_SAI_AUDIO_H

#include <stdint.h>

/* Frame (stereo L+R sample pair) count per DMA half-buffer. */
#ifndef AUDIO_HALF_FRAMES
#define AUDIO_HALF_FRAMES   2048U
#endif

/**
  * @brief  Initialise SAI1_A + DMA + SDRAM double-buffer (no transfer yet).
  * @param  sample_rate  nominal rate (44100 / 48000 typical)
  * @param  channels    forced to 2 (stereo) internally
  * @param  bits        forced to 16 internally
  * @retval 0 ok, -1 fail
  */
int sai_audio_init(uint32_t sample_rate, uint8_t channels, uint8_t bits);

/**
  * @brief  Re-configure the SAI clock/dividers for a new sample rate without
  *         re-allocating the buffer.  Call while stopped.
  */
int sai_audio_configure(uint32_t sample_rate);

/** @brief  Start (or restart) the circular DMA transfer. */
void sai_audio_start(void);

/** @brief  Stop the DMA transfer (pause / track switch). */
void sai_audio_stop(void);

/** @brief  Pointer to the SDRAM double-buffer (uint16_t, stereo interleaved). */
uint16_t *sai_audio_buffer(void);

/** @brief  Number of stereo frames in one DMA half. */
uint32_t sai_audio_half_frames(void);

/**
  * @brief  Block until the DMA signals that one half-buffer needs refilling,
  *         and return which half (0 or 1) to write next.
  */
uint8_t sai_audio_get_empty_half(void);

/**
  * @brief  Discard any pending "half empty" signal(s).  Called by the player
  *         on pause/stop so a stale DMA-complete notification cannot make the
  *         refill loop write to a half that is already being played after a
  *         restart.
  */
void sai_audio_drain(void);

#endif /* __BSP_SAI_AUDIO_H */
