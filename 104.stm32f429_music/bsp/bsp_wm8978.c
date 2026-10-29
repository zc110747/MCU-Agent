/**
  ******************************************************************************
  * @file    bsp_wm8978.c
  * @brief   WM8978 control over the bit-banged I2C bus (PH4/PH5).
  *
  *  Ported from the ALIENTEK 音乐播放 experiment (HARDWARE/WM8978/wm8978.c).
  *  The register sequences are kept identical to the reference; only the I2C
  *  transport changed (bit-banged single/multi-byte write instead of the
  *  ALIENTEK myiic.c macros) and the API was trimmed to what the player uses.
  *
  *  Playback chain this file configures:
  *      SAI1_A (I2S master TX) -> WM8978 DAC -> HP / SPEAKER
  ******************************************************************************
  */
#include "bsp_wm8978.h"
#include "bsp_iic.h"

#define WM8978_REG_COUNT    58U

/* Shadow of the 9-bit register file (R0..R57). */
static uint16_t s_reg[WM8978_REG_COUNT] =
{
  0x0000, 0x0000, 0x0000, 0x0000, 0x0050, 0x0000, 0x0140, 0x0000,
  0x0000, 0x0000, 0x0000, 0x00FF, 0x00FF, 0x0000, 0x0100, 0x00FF,
  0x00FF, 0x0000, 0x012C, 0x002C, 0x002C, 0x002C, 0x002C, 0x0000,
  0x0032, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0038, 0x000B, 0x0032, 0x0000, 0x0008, 0x000C, 0x0093, 0x00E9,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0003, 0x0010, 0x0010, 0x0100,
  0x0100, 0x0002, 0x0001, 0x0001, 0x0039, 0x0039, 0x0039, 0x0039,
  0x0001, 0x0001
};

uint8_t bsp_wm8978_write_reg(uint8_t reg, uint16_t value)
{
  uint8_t payload[2];

  if (reg >= WM8978_REG_COUNT)
  {
    return 3U;
  }

  /* R7 register address + the 9th data bit, then the low byte. */
  payload[0] = (uint8_t)((reg << 1) | ((value >> 8) & 0x01U));
  payload[1] = (uint8_t)(value & 0xFFU);

  if (!bsp_iic_write_bytes(WM8978_I2C_ADDR, payload, 2U))
  {
    return 2U;
  }

  s_reg[reg] = value;
  return 0U;
}

uint16_t bsp_wm8978_read_reg(uint8_t reg)
{
  if (reg >= WM8978_REG_COUNT)
  {
    return 0U;
  }
  return s_reg[reg];
}

uint8_t bsp_wm8978_init(void)
{
  bsp_iic_init();

  if (bsp_wm8978_write_reg(0U, 0U) != 0U)   /* software reset */
  {
    return 1U;
  }

  bsp_wm8978_write_reg(1U, 0x1B);    /* MICEN, BIASEN, VMIDSEL = 11 (5k)   */
  bsp_wm8978_write_reg(2U, 0x1B0);   /* ROUT1/LOUT1 + boost enabled        */
  bsp_wm8978_write_reg(3U, 0x6C);    /* LOUT2/ROUT2 (speaker) + mixers     */
  bsp_wm8978_write_reg(6U, 0x00);    /* MCLK from the SAI, not PLL         */
  bsp_wm8978_write_reg(43U, 1U << 4);/* INVROUT2: non-inverting speaker    */
  bsp_wm8978_write_reg(47U, 1U << 8);/* PGA boost L +20 dB                */
  bsp_wm8978_write_reg(48U, 1U << 8);/* PGA boost R +20 dB                */
  bsp_wm8978_write_reg(49U, 1U << 1);/* thermal shutdown                   */
  bsp_wm8978_write_reg(49U, 1U << 2);/* speaker boost 1.5x                 */
  bsp_wm8978_write_reg(10U, 1U << 3);/* DAC oversampling 128x              */
  bsp_wm8978_write_reg(14U, 1U << 3);/* ADC oversampling 128x              */
  return 0U;
}

void bsp_wm8978_set_dac(uint8_t dac_enable)
{
  uint16_t value;

  /* R3 bit1:0 -> DACL/DACR */
  value = bsp_wm8978_read_reg(3U);
  if (dac_enable != 0U)
  {
    value |= 3U << 0;
  }
  else
  {
    value &= (uint16_t)~(3U << 0);
  }
  bsp_wm8978_write_reg(3U, value);

  /* R2 bit1:0 -> ADCL/ADCR.  This player only ever plays back, so the record
   * path is held off unconditionally. */
  value = bsp_wm8978_read_reg(2U) & (uint16_t)~(3U << 0);
  bsp_wm8978_write_reg(2U, value);
}

void bsp_wm8978_set_output(uint8_t dac_to_output)
{
  uint16_t value = 0U;

  if (dac_to_output != 0U)
  {
    value |= 1U << 0;   /* DAC -> output mixer */
    value |= 1U << 1;   /* bypass off          */
    value |= 5U << 2;   /* 0 dB                */
  }
  bsp_wm8978_write_reg(50U, value);
  bsp_wm8978_write_reg(51U, value);
}

void bsp_wm8978_set_i2s_format(uint8_t fmt, uint8_t word_len)
{
  uint16_t value = (uint16_t)(((fmt & 0x03U) << 3) | ((word_len & 0x03U) << 5));
  bsp_wm8978_write_reg(4U, value);
}

void bsp_wm8978_set_hp_volume(uint8_t left, uint8_t right)
{
  uint8_t voll = (uint8_t)(left & 0x3FU);
  uint8_t volr = (uint8_t)(right & 0x3FU);

  if (voll == 0U)
  {
    voll |= 1U << 6;    /* 0 means mute, not "silence by attenuation" */
  }
  if (volr == 0U)
  {
    volr |= 1U << 6;
  }

  bsp_wm8978_write_reg(52U, voll);
  bsp_wm8978_write_reg(53U, (uint16_t)(volr | (1U << 8)));   /* HPVU */
}

void bsp_wm8978_set_spk_volume(uint8_t volume)
{
  uint8_t vol = (uint8_t)(volume & 0x3FU);

  if (vol == 0U)
  {
    vol |= 1U << 6;
  }

  bsp_wm8978_write_reg(54U, vol);
  bsp_wm8978_write_reg(55U, (uint16_t)(vol | (1U << 8)));    /* SPKVU */
}

void bsp_wm8978_set_eq3d(uint8_t depth)
{
  bsp_wm8978_write_reg(41U, (uint16_t)(depth & 0x0FU));
}
