#include "bsp_pcf8574.h"
#include "bsp_i2c.h"
#include "bsp_delay.h"

/**
  * @brief  Write a single byte to the PCF8574 expander.
  */
HAL_StatusTypeDef BSP_PCF8574_Write(uint8_t data)
{
  return HAL_I2C_Master_Transmit(&hi2c2, (uint16_t)(PCF8574_ADDR << 1U),
                                 &data, 1U, 100U);
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

  /* Release reset (P7 = 0 -> ETH_RESET high) */
  BSP_PCF8574_Write(0x7FU);
  bsp_delay_ms(50U);
}
