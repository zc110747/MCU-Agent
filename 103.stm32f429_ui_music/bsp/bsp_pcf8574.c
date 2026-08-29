#include "bsp_pcf8574.h"
#include "bsp_i2c.h"
#include "bsp_delay.h"

/**
  * @brief  Write a single byte to the PCF8574 expander.
  */
HAL_StatusTypeDef BSP_PCF8574_Write(uint8_t data)
{
  HAL_StatusTypeDef st;

  BSP_I2C_Lock();
  st = HAL_I2C_Master_Transmit(&hi2c2, (uint16_t)(PCF8574_ADDR << 1U),
                                &data, 1U, 100U);
  BSP_I2C_Unlock();
  return st;
}

/**
  * @brief  Release the LAN8720 PHY from reset.
  *
  * ETH_RESET is driven by PCF8574 P7 through a transistor:
  *   P7 = 1 -> ETH_RESET = 0 (reset active)
  *   P7 = 0 -> ETH_RESET = 1 (normal operation)
  * We first assert reset, then release it.
  */
void BSP_ETH_PHY_Reset(void)
{
  /* Assert reset (P7 = 1, all other lines high/input) */
  BSP_PCF8574_Write(0xFFU);
  bsp_delay_ms(50U);

  /* Release reset (P7 = 0 -> ETH_RESET high), BEEP off */
  BSP_PCF8574_Write(0x7FU);
  bsp_delay_ms(50U);
}

/**
  * @brief  BEEP control: PCF8574 P0, LOW = sound (confirmed by user).
  *         We track the current expander byte and only flip P0.
  */
static uint8_t g_pcf8574_state = 0x7FU;   /* matches state left by PHY reset (P0=H -> silent) */

void BSP_BEEP_Set(uint8_t on)
{
  if (on)
  {
    /* sound: drive P0 LOW (buzzer sinks current to ground) */
    g_pcf8574_state &= (uint8_t)~(1U << PCF8574_BEEP_IO);
  }
  else
  {
    /* silent: release P0 HIGH (pulled up, no current) */
    g_pcf8574_state |= (uint8_t)(1U << PCF8574_BEEP_IO);
  }
  BSP_PCF8574_Write(g_pcf8574_state);
}

void BSP_BEEP_On(void)
{
  BSP_BEEP_Set(1);
}

void BSP_BEEP_Off(void)
{
  BSP_BEEP_Set(0);
}
