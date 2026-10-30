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
#include "bsp_delay.h"

#define WM8978_REG_COUNT 58U

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
        0x0001, 0x0001};

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

    if (bsp_wm8978_write_reg(0U, 0U) != 0U) /* software reset */
    {
        return 1U;
    }

    /* Mute everything *before* any analog block comes up.  At this point the
     * SAI is not configured yet, so there is no MCLK: a DAC that is enabled
     * without a clock free-runs on noise and the speaker reproduces it, which
     * is exactly the burst heard at every power-on. */
    bsp_wm8978_set_dac(0U);
    bsp_wm8978_set_output(0U);
    bsp_wm8978_set_hp_volume(0U, 0U);
    bsp_wm8978_set_spk_volume(0U);

    bsp_wm8978_write_reg(1U, 0x1B);     /* MICEN, BIASEN, VMIDSEL = 11 (5k)   */
    bsp_delay_ms(50U);                  /* let VMID / bias settle before the
                                         * output stages are switched on      */
    bsp_wm8978_write_reg(2U, 0x1B0);    /* ROUT1/LOUT1 + boost enabled        */
    bsp_wm8978_write_reg(3U, 0x6C);     /* LOUT2/ROUT2 (speaker) + mixers     */
    bsp_delay_ms(50U);
    bsp_wm8978_write_reg(6U, 0x00);     /* MCLK from the SAI, not PLL         */
    bsp_wm8978_write_reg(43U, 1U << 4); /* INVROUT2: non-inverting speaker    */
    bsp_wm8978_write_reg(47U, 1U << 8); /* PGA boost L +20 dB                */
    bsp_wm8978_write_reg(48U, 1U << 8); /* PGA boost R +20 dB                */
    bsp_wm8978_write_reg(49U, 1U << 1); /* thermal shutdown                   */
    bsp_wm8978_write_reg(49U, 1U << 2); /* speaker boost 1.5x                 */
    bsp_wm8978_write_reg(10U, 1U << 3); /* DAC oversampling 128x              */
    bsp_wm8978_write_reg(14U, 1U << 3); /* ADC oversampling 128x              */

    /* R2/R3 just enabled the output stages; make sure the signal path is still
     * shut so the codec stays silent until the first real PCM buffer plays. */
    bsp_wm8978_set_dac(0U);
    bsp_wm8978_set_output(0U);
    bsp_wm8978_set_hp_volume(0U, 0U);
    bsp_wm8978_set_spk_volume(0U);
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
        value |= 1U << 0; /* DAC -> output mixer */
        value |= 1U << 1; /* bypass off          */
        value |= 5U << 2; /* 0 dB                */
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
        voll |= 1U << 6; /* 0 means mute, not "silence by attenuation" */
    }
    if (volr == 0U)
    {
        volr |= 1U << 6;
    }

    bsp_wm8978_write_reg(52U, voll);
    bsp_wm8978_write_reg(53U, (uint16_t)(volr | (1U << 8))); /* HPVU */
}

void bsp_wm8978_set_spk_volume(uint8_t volume)
{
    uint8_t vol = (uint8_t)(volume & 0x3FU);

    if (vol == 0U)
    {
        vol |= 1U << 6;
    }

    bsp_wm8978_write_reg(54U, vol);
    bsp_wm8978_write_reg(55U, (uint16_t)(vol | (1U << 8))); /* SPKVU */
}

void bsp_wm8978_set_eq3d(uint8_t depth)
{
    bsp_wm8978_write_reg(41U, (uint16_t)(depth & 0x0FU));
}

/* -------------------------------------------------------------------------- */
/* Volume ladder (KEY1)                                                       */
/* -------------------------------------------------------------------------- */
/* 6-bit code -> gain: 0 = -57 dB, 57 = 0 dB, 63 = +6 dB (1 dB per step).
 *   45 -> -12 dB   51 -> -6 dB   55 -> -2 dB   58 -> +1 dB   61 -> +4 dB */
static const uint8_t s_volume_ladder[WM8978_VOLUME_LEVELS] = {
    45U, 51U, 55U, 58U, 61U};

typedef struct
{
    uint8_t level; /* 1-based index into s_volume_ladder */
    int8_t  dir;   /* +1 while climbing, -1 while descending */
} WM8978_VOLUME_STATE_T;

static WM8978_VOLUME_STATE_T s_volume = {WM8978_VOLUME_LEVEL_DEF, 1};

void bsp_wm8978_set_volume_level(uint8_t level)
{
    uint8_t code;

    if (level < WM8978_VOLUME_LEVEL_MIN)
    {
        level = WM8978_VOLUME_LEVEL_MIN;
    }
    if (level > WM8978_VOLUME_LEVELS)
    {
        level = WM8978_VOLUME_LEVELS;
    }

    s_volume.level = level;
    code           = s_volume_ladder[level - 1U];

    /* Headphone (R52/R53) and speaker (R54/R55) follow the same ladder: on this
     * board both are active, and keeping them equal avoids a level jump when
     * the headphones are plugged in. */
    bsp_wm8978_set_hp_volume(code, code);
    bsp_wm8978_set_spk_volume(code);
}

uint8_t bsp_wm8978_volume_level(void)
{
    return s_volume.level;
}

uint8_t bsp_wm8978_volume_step(void)
{
    int8_t next = (int8_t)s_volume.level + s_volume.dir;

    if (next > (int8_t)WM8978_VOLUME_LEVELS)
    {
        /* Clamp at the top and turn round: 5 -> 4, never 5 -> 1. */
        s_volume.dir = -1;
        next         = (int8_t)WM8978_VOLUME_LEVELS - 1;
    }
    else if (next < (int8_t)WM8978_VOLUME_LEVEL_MIN)
    {
        /* Same at the bottom: 1 -> 2. */
        s_volume.dir = 1;
        next         = (int8_t)WM8978_VOLUME_LEVEL_MIN + 1;
    }

    bsp_wm8978_set_volume_level((uint8_t)next);
    return s_volume.level;
}

/* -------------------------------------------------------------------------- */
/* Output gate                                                                */
/* -------------------------------------------------------------------------- */
/**
 * @brief  Close the signal path: volumes to mute, then mixer, then DAC.
 *
 *  Called at power-on and every time the stream stops.  Order matters: the DAC
 *  and the output mixer must never be left running without MCLK (the board's
 *  speaker turns that into a burst of noise) and the mixer must not be opened
 *  on a stale DC level.
 */
void bsp_wm8978_mute(void)
{
    bsp_wm8978_set_hp_volume(0U, 0U);
    bsp_wm8978_set_spk_volume(0U);
    bsp_wm8978_set_output(0U);
    bsp_wm8978_set_dac(0U);
}

/**
 * @brief  Open the signal path again at the last volume level.
 *
 *  Must only be called once MCLK runs and the SAI is fed with real samples
 *  (after bsp_sai_play()), otherwise the DAC converts whatever the empty FIFO
 *  holds.
 */
void bsp_wm8978_unmute(void)
{
    bsp_wm8978_set_dac(1U);
    bsp_wm8978_set_output(1U);
    bsp_wm8978_set_volume_level(s_volume.level);
}
