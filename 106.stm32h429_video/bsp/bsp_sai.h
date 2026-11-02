/**
 ******************************************************************************
 * @file    bsp_sai.h
 * @brief   SAI1 block A as an I2S master transmitter, feeding the WM8978 DAC
 *          through DMA2 Stream3 in double-buffer (ping-pong) mode.
 *
 *          Data path:
 *              app buffers -> DMA2_Stream3 (circular, M0AR/M1AR) -> SAI1_A_DR
 *                          -> PE2..PE6 (AF6) -> WM8978 -> speaker / HP
 *
 *          The transfer-complete interrupt does NOT refill anything.  It only
 *          runs the registered callback, which lives in the application and
 *          typically releases a semaphore.  Filling from the ISR would block on
 *          FatFs, which is not interrupt-safe.
 ******************************************************************************
 */
#ifndef __BSP_SAI_H
#define __BSP_SAI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Audio sample width as stored in the WAV file (bits per sample). */
#define BSP_SAI_BITS_16 16U
#define BSP_SAI_BITS_24 24U

/**
 * @brief  Configure SAI1_A + PLLI2S for a given sample width / rate and arm the
 *         DMA with two buffers.
 *
 * @param  bits      BSP_SAI_BITS_16 or BSP_SAI_BITS_24.  For 24-bit audio the
 *                   caller must supply samples left-aligned in 32-bit words
 *                   (4 bytes per sample) and pass 4 as width_bytes.
 * @param  rate      sample rate in Hz; must be one of the rates supported by
 *                   the PLLI2S table (8k..192k, 44.1k family included).
 * @param  buf0      first half buffer (DMA M0AR)
 * @param  buf1      second half buffer (DMA M1AR)
 * @param  bytes     size of each buffer in bytes
 *
 * @retval true  configured and armed (still stopped; call bsp_sai_play())
 * @retval false unsupported rate, or HAL refused the configuration
 */
bool bsp_sai_configure(uint8_t bits, uint32_t rate,
                       uint8_t *buf0, uint8_t *buf1, uint16_t bytes);

/**
 * @brief  Register the transfer-complete callback (runs in interrupt context,
 *         priority 6, so FromISR APIs are allowed).
 */
void bsp_sai_set_dma_callback(void (*callback)(void));

/** Start / stop the DMA stream.  The SAI clock keeps running in both states. */
void bsp_sai_play(void);
void bsp_sai_stop(void);

/**
 * @brief  Which buffer did the DMA just finish with?
 * @retval 0 -> buf0 is free (refill it), 1 -> buf1 is free.
 *
 *         Valid inside the transfer-complete callback.  Read the DMA's CT bit,
 *         not a software counter: the counter drifts after a stop/start.
 */
uint8_t bsp_sai_free_buffer(void);

/** DMA2 Stream3 interrupt body, called from stm32f4xx_it.c. */
void bsp_sai_dma_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SAI_H */
