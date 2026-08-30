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
 *  CLOCKING (verified against Drivers/.../stm32f4xx_hal_sai.c)
 *    FS   = SAI_CK / (MCKDIV * 512)     -- hal_sai.c:456/465
 *    MCLK = SAI_CK / (MCKDIV * 2) = 256 * FS
 *    BCLK = FS * 32                     (I2S, 16-bit stereo)
 *
 *  SAI_CK comes from PLLSAI_N / (PLLSAI_Q * PLLSAIDivQ) with a fixed 1 MHz
 *  VCO input (HSE 25 MHz / PLLM 25).  PLLSAI feeds ONLY the SAI here - the
 *  48 MHz USB/SDIO clock comes from the main PLL's PLLQ and LTDC is not
 *  enabled - so N/Q/DivQ may be reprogrammed per sample rate.  The driver
 *  picks the combination with the smallest achievable error; see
 *  tools/audio/sai_clock_search.py.  Result: <= 0.019 % error (about 0.3
 *  cents) for 8/11.025/16/22.05/24/32/44.1/48 kHz.
 *
 *  NOTE: 48 MHz SAI_CK (the previous setting) cannot express 44.1 k or 48 k
 *  at all - it collapses both to 46 875 Hz (+6.29 % / -2.34 %).
 ******************************************************************************
 */
#ifndef __BSP_SAI_AUDIO_H
#define __BSP_SAI_AUDIO_H

#include <stdint.h>

/* Frame (stereo L+R sample pair) count per DMA half-buffer. */
#ifndef AUDIO_HALF_FRAMES
#define AUDIO_HALF_FRAMES   2048U
#endif

/* Clock telemetry, updated by the driver and readable over SWD so the clock
 * tree can be confirmed without a scope or a UART.  g_sai_fs_measured_hz is
 * derived from the real DMA refill rate against free-running TIM2, i.e. it is
 * the true sample rate the CODEC is being driven at. */
extern volatile uint32_t g_sai_saick_hz;        /* SAI kernel clock, Hz     */
extern volatile uint32_t g_sai_mclk_hz;         /* MCLK (PE2), Hz           */
extern volatile uint32_t g_sai_bclk_hz;         /* BCLK (PE5), Hz           */
extern volatile uint32_t g_sai_mckdiv;          /* SAI CR1 MCKDIV[3:0]      */
extern volatile uint32_t g_sai_fs_target_hz;    /* requested sample rate    */
extern volatile uint32_t g_sai_fs_measured_hz;  /* measured sample rate     */
extern volatile uint32_t g_sai_tx_dma_ret;       /* last HAL_SAI_Transmit_DMA ret */

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
