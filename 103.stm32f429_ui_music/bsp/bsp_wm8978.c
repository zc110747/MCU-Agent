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

/* ---- Register addresses (WM8978, 7-bit) ---------------------------------- */
#define R0_SOFT_RESET   0x00U
#define R1_POWER1       0x01U
#define R2_POWER2       0x02U
#define R3_POWER3       0x03U
#define R4_POWER4       0x04U
#define R6_CLOCK1       0x06U
#define R10_IFACE       0x0AU
#define R43_ROUT2MIX    0x2BU
#define R47_LOUT1MIX    0x2FU
#define R49_ROUT1MIX    0x31U
#define R48_LOUT1VOL    0x30U
#define R50_ROUT1VOL    0x32U
#define R51_ROUT2VOL    0x33U
#define R52_LDAC_VOL    0x34U
#define R53_RDAC_VOL    0x35U

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

    /* Power management 1: VREF + VMID (bias on). */
    rc |= wm8978_write(R1_POWER1, 0x01FU);

    /* Power management 2: enable all 6 output drivers
     * (LOUT1/ROUT1 headphone, LOUT2/ROUT2 speaker, SPKL/SPKR). */
    rc |= wm8978_write(R2_POWER2, 0x1B7U);

    /* Power management 3: enable DACs + output mixers. */
    rc |= wm8978_write(R3_POWER3, 0x1ECU);

    /* Clock control 1: use MCLK pin (no PLL), slave. */
    rc |= wm8978_write(R4_POWER4, 0x000U);
    rc |= wm8978_write(R6_CLOCK1, 0x000U);

    /* Audio interface: I2S format (FMT=10), 16-bit word length (WL=00). */
    rc |= wm8978_write(R10_IFACE, 0x002U);

    /* Output routing: connect DAC to the headphone mixers (bit8 of each
     * mixer = DACx2OUTx) and to the speaker mixer (bits3/4 of ROUT2MIX). */
    rc |= wm8978_write(R47_LOUT1MIX, 0x100U);  /* LDAC -> LOUT1 */
    rc |= wm8978_write(R49_ROUT1MIX, 0x100U);  /* RDAC -> ROUT1 */
    rc |= wm8978_write(R43_ROUT2MIX, 0x018U);  /* L/R mix -> ROUT2 (speaker) */

    /* Analogue output volumes: ~0 dB (6-bit, 0x3F). */
    rc |= wm8978_write(R48_LOUT1VOL, 0x03FU);
    rc |= wm8978_write(R50_ROUT1VOL, 0x03FU);
    rc |= wm8978_write(R51_ROUT2VOL, 0x03FU);

    /* Digital DAC volume: 0xFF = 0 dB (unmuted). The UI volume rides on this. */
    rc |= wm8978_write(R52_LDAC_VOL, 0x0FFU);
    rc |= wm8978_write(R53_RDAC_VOL, 0x0FFU);

    if (rc != 0)
    {
        PRINT_LOG("[WM8978] init reported I2C errors (rc=%d)\r\n", rc);
        return -1;
    }

    /* Apply the stored (default max) volume. */
    wm8978_set_volume(s_vol);
    PRINT_LOG("[WM8978] init OK (I2S slave, DAC->HP+SPK)\r\n");
    return 0;
}

int wm8978_set_volume(uint8_t vol)
{
    if (vol > WM8978_VOL_MAX) vol = WM8978_VOL_MAX;
    s_vol = vol;

    /* Map 0..100 to the 8-bit digital DAC volume (0x00 = mute, 0xFF = 0 dB).
     * Linear in code value; not strictly dB-linear but smooth enough for a UI. */
    uint8_t v = (vol == 0U) ? 0x00U : (uint8_t)((vol * 255U) / 100U);
    int rc = 0;
    rc |= wm8978_write(R52_LDAC_VOL, v);
    rc |= wm8978_write(R53_RDAC_VOL, v);
    return rc;
}

uint8_t wm8978_get_volume(void)
{
    return s_vol;
}
