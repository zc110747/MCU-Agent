/**
 ******************************************************************************
 * @file    bsp_wm8978.h
 * @brief   WM8978 audio codec control (I2C side only; the audio data path is
 *          SAI1 block A, see bsp_sai.h).
 *
 *          Register values are shadowed in RAM because the WM8978 I2C
 *          interface is write-only: every "read-modify-write" helper below
 *          works on the shadow table, not on the chip.
 ******************************************************************************
 */
#ifndef __BSP_WM8978_H
#define __BSP_WM8978_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WM8978_I2C_ADDR 0x1AU /* fixed by the chip */

/* I2S data format (register R4 FORMAT field) */
#define WM8978_FMT_LSB 0U
#define WM8978_FMT_MSB 1U
#define WM8978_FMT_I2S 2U
#define WM8978_FMT_PCM 3U

/* Word length (register R4 WL field) */
#define WM8978_WLEN_16 0U
#define WM8978_WLEN_20 1U
#define WM8978_WLEN_24 2U
#define WM8978_WLEN_32 3U

/**
 * @brief  Reset the codec and apply the playback-only configuration.
 *
 *  The output path is left MUTED (DAC off, mixer off, volumes muted) so the
 *  codec stays silent while the SAI has no clock.  Playback opens it through
 *  bsp_wm8978_unmute() once real PCM data is running.
 *
 * @retval 0 on success, 1 if the chip did not acknowledge the reset.
 */
uint8_t bsp_wm8978_init(void);

/**
 * @brief  Close the signal path (volumes muted, mixer off, DAC off).
 *         Call before stopping the SAI clock.
 */
void bsp_wm8978_mute(void);

/**
 * @brief  Open the signal path at the current volume level.
 *         Call only after bsp_sai_play(), i.e. once MCLK and PCM data are up.
 */
void bsp_wm8978_unmute(void);

uint8_t  bsp_wm8978_write_reg(uint8_t reg, uint16_t value);
uint16_t bsp_wm8978_read_reg(uint8_t reg);

void bsp_wm8978_set_dac(uint8_t dac_enable);
void bsp_wm8978_set_output(uint8_t dac_to_output);
void bsp_wm8978_set_i2s_format(uint8_t fmt, uint8_t word_len);
void bsp_wm8978_set_hp_volume(uint8_t left, uint8_t right);
void bsp_wm8978_set_spk_volume(uint8_t volume);
void bsp_wm8978_set_eq3d(uint8_t depth);

/* Volume ladder driven by KEY1.
 *
 * The codec's 6-bit volume field runs 0 = -57 dB, 57 = 0 dB, 63 = +6 dB.  The
 * ladder below spans 1 = -12 dB .. 5 = +4 dB and deliberately stops one code
 * short of the maximum, where the on-board speaker starts to clip.
 *
 * Stepping bounces at both ends (1 -> 5, then 5 -> 1) instead of wrapping, so a
 * single key press can never jump from the loudest setting straight to the
 * quietest one. */
#define WM8978_VOLUME_LEVELS 5U
#define WM8978_VOLUME_LEVEL_MIN 1U
#define WM8978_VOLUME_LEVEL_DEF 3U

/** Apply a 1-based level; values outside 1..WM8978_VOLUME_LEVELS are clamped. */
void bsp_wm8978_set_volume_level(uint8_t level);

/** Current 1-based volume level. */
uint8_t bsp_wm8978_volume_level(void);

/** Advance one step (bouncing at both ends) and return the new level. */
uint8_t bsp_wm8978_volume_step(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_WM8978_H */
