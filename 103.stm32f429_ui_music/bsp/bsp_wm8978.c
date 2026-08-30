/**
  ******************************************************************************
  * @file    bsp/bsp_wm8978.c
  * @brief   WM8978 control register programming over I2C2 (see bsp_wm8978.h).
  ******************************************************************************
  */
#include "bsp_wm8978.h"
#include "bsp_i2c.h"
#include "stm32f4xx_hal.h"
#include "log.h"

#include <string.h>

/* I2C2 handle (PH4/PH5) is owned by bsp_i2c.c. */
extern I2C_HandleTypeDef hi2c2;

/* ---- Register addresses (WM8978, 7-bit addr / 9-bit val) ------------------ */
#define R0_SOFT_RESET   0x00U
#define R1_POWER1       0x01U
#define R2_POWER2       0x02U
#define R3_POWER3       0x03U
#define R4_AIFACE       0x04U   /* Audio Interface Control (I2S fmt + word len) */
#define R6_CLOCK1       0x06U   /* Clock Control (MCLK source) */
#define R10_DACCTRL     0x0AU   /* DAC Control (soft-mute, oversampling) */
#define R11_LDAC_VOL    0x0BU   /* Left DAC digital volume (8-bit, 0xFF = 0 dB) */
#define R12_RDAC_VOL    0x0CU   /* Right DAC digital volume (8-bit) */
#define R14_ADCCTRL     0x0EU   /* ADC Control (oversampling) */
#define R43_ROUT2MIX    0x2BU   /* ROUT2 (speaker) mixer */
#define R47_LOUT1MIX    0x2FU   /* LOUT1 mixer: bit8 = LDAC2LMIX (DAC->HP-L) */
#define R48_ROUT1MIX    0x30U   /* ROUT1 mixer: bit8 = RDAC2RMIX (DAC->HP-R) */
#define R49_SPKCTRL     0x31U   /* bit1 = TSDEN, bit2 = SPEAKERBOOST(1.5x) */
#define R50_LOUT2VOL    0x32U   /* speaker-L volume / DAC-out enable (bit0) */
#define R51_ROUT2VOL    0x33U   /* speaker-R volume / DAC-out enable (bit0) */
#define R52_HPVOL_L     0x34U   /* headphone-L analog volume */
#define R53_HPVOL_R     0x35U   /* headphone-R analog volume */

/* Current volume (0..100), for the getter. */
static uint8_t s_vol = WM8978_VOL_MAX;

/**
  * @brief  Write a 9-bit WM8978 register.
  *         I2C frame: [reg<<1 | data_hi] [data_lo]  (2 bytes).
  */
static int wm8978_write(uint8_t reg, uint16_t val)
{
    uint8_t tx[2];
    tx[0] = (uint8_t)(((reg & 0x7FU) << 1U) | ((val >> 8U) & 0x01U));
    tx[1] = (uint8_t)(val & 0xFFU);

    BSP_I2C_Lock();
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c2, WM8978_I2C_ADDR,
                                                   tx, 2U, 50U);
    BSP_I2C_Unlock();

    if (st != HAL_OK)
    {
        PRINT_LOG("[WM8978] I2C write reg 0x%02X failed (%d)\r\n", reg, st);
        return -1;
    }
    return 0;
}

int wm8978_init(void)
{
    int rc = 0;

    /* Software reset. */
    rc |= wm8978_write(R0_SOFT_RESET, 0x000U);

    /* Power management 1: BIASEN + VMIDSEL(5k) + MICEN (matches 正点原子 ref). */
    rc |= wm8978_write(R1_POWER1, 0x01BU);
    /* Power management 2: headphone/speaker + BOOST drivers (ROUT2/LOUT2). */
    rc |= wm8978_write(R2_POWER2, 0x1B0U);
    /* Power management 3: output mixers + speaker, AND the DAC (bits 0,1).
     * The DAC MUST be powered or there is no audio.  The 正点原子 reference
     * enables the DAC here through WM8978_ADDA_Cfg(1,0) (R3 |= 0x03); we fold
     * it into the init so playback works standalone. */
    rc |= wm8978_write(R3_POWER3, 0x06FU);
    /* Clock control 1: MCLK from pin (external), no PLL; CODEC is I2S slave. */
    rc |= wm8978_write(R6_CLOCK1, 0x000U);

    /* Audio Interface: PHILIPS I2S, 16-bit word length.
     * R4 = (FMT<<3) | (WL<<5); FMT=2 (I2S), WL=0 (16-bit) -> 0x10.
     * THIS is the register the SAI (I2S master) must agree with.  The old code
     * wrote the I2S format into R10 (DAC Control) and left R4=0x00 (right-
     * justified 16-bit) -> the CODEC and SAI spoke different wire formats, so
     * playback was silent/garbled.  Reference: WM8978_I2S_Cfg(2,0). */
    rc |= wm8978_write(R4_AIFACE, 0x010U);

    /* DAC Control: soft-mute OFF + 128x oversampling (best SNR). */
    rc |= wm8978_write(R10_DACCTRL, 0x008U);
    /* ADC Control: 128x oversampling (unused for playback, harmless). */
    rc |= wm8978_write(R14_ADCCTRL, 0x008U);

    /* Output routing: DAC -> headphone mixers, and mixers -> speaker. */
    rc |= wm8978_write(R47_LOUT1MIX, 0x100U); /* LDAC -> LOUT1 mixer */
    rc |= wm8978_write(R48_ROUT1MIX, 0x100U); /* RDAC -> ROUT1 mixer */
    rc |= wm8978_write(R43_ROUT2MIX, 0x010U); /* L/R mix -> ROUT2 (speaker) */
    rc |= wm8978_write(R49_SPKCTRL, 0x006U);  /* TSDEN + SPEAKERBOOST 1.5x */

    /* Enable DAC output to the output stage (bit0 of the OUT2 vol regs). */
    rc |= wm8978_write(R50_LOUT2VOL, 0x001U);
    rc |= wm8978_write(R51_ROUT2VOL, 0x001U);

    /* DAC digital volume: 0xFF = 0 dB (unmuted).  The UI volume rides on
     * R11/R12 via wm8978_set_volume(). */
    rc |= wm8978_write(R11_LDAC_VOL, 0x0FFU);
    rc |= wm8978_write(R12_RDAC_VOL, 0x0FFU);

    /* Headphone analogue volume: ~0 dB (6-bit, 0x3F). */
    rc |= wm8978_write(R52_HPVOL_L, 0x03FU);
    rc |= wm8978_write(R53_HPVOL_R, 0x03FU);

    if (rc != 0)
    {
        PRINT_LOG("[WM8978] init reported I2C errors (rc=%d)\r\n", rc);
        return -1;
    }

    /* Apply the stored (default) volume via the digital DAC path. */
    wm8978_set_volume(s_vol);
    PRINT_LOG("[WM8978] init OK (I2S slave, R4=0x10 I2S/16b, DAC->HP+SPK)\r\n");
    return 0;
}

int wm8978_set_volume(uint8_t vol)
{
    if (vol > WM8978_VOL_MAX) vol = WM8978_VOL_MAX;
    s_vol = vol;

    /* Map 0..100 to the 8-bit digital DAC volume (R11/R12): 0x00 = mute,
     * 0xFF = 0 dB.  Linear in code value; smooth enough for a UI.  (R11/R12
     * are the DAC digital-volume registers; the headphone analogue registers
     * R52/R53 are fixed at 0 dB in wm8978_init().) */
    uint8_t v = (vol == 0U) ? 0x00U : (uint8_t)((vol * 255U) / 100U);
    int rc = 0;
    rc |= wm8978_write(R11_LDAC_VOL, v);
    rc |= wm8978_write(R12_RDAC_VOL, v);
    return rc;
}

uint8_t wm8978_get_volume(void)
{
    return s_vol;
}
