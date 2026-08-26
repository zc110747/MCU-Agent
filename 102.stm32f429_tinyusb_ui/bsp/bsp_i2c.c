#include "bsp_i2c.h"

/* Defined in main.c */
void Error_Handler(void);

I2C_HandleTypeDef hi2c2;

/**
  * @brief  Initialize I2C2 on PH4(SCL)/PH5(SDA) at 100 kHz (standard mode).
  */
void BSP_I2C_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_I2C2_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  GPIO_InitStruct.Pin       = GPIO_PIN_4 | GPIO_PIN_5;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;        /* open-drain for I2C */
  GPIO_InitStruct.Pull      = GPIO_PULLUP;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  hi2c2.Instance             = I2C2;
  hi2c2.Init.ClockSpeed      = 100000;
  hi2c2.Init.DutyCycle       = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1     = 0;
  hi2c2.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2     = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* ---- I2C bus recovery ---------------------------------------------------- */
/* Minimal delay so we don't depend on HAL_Delay (which may be blocked if the
 * caller is inside a critical section or the scheduler isn't running). */
static void i2c_udelay(volatile uint32_t us)
{
  /* SYSCLK = 168 MHz; ~7 cycles per loop iteration (conservative). */
  volatile uint32_t cycles = us * 18U;
  while (cycles--) { __NOP(); }
}

static void i2c_gpio_af_od(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin       = GPIO_PIN_4 | GPIO_PIN_5;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull      = GPIO_PULLUP;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);
}

static void i2c_scl_gpio_out(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* SCL as push-pull output so we can clock the bus manually.
   * SDA stays as the I2C open-drain AF (we only read it to detect a stuck slave). */
  GPIO_InitStruct.Pin       = GPIO_PIN_4;
  GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);
}

int BSP_I2C_Recover(void)
{
  /* 1. Decide whether recovery is even needed.
   *    A stuck bus shows up as: SDA held LOW by a slave, or the peripheral
   *    still reporting BUSY / a latched error flag (AF/ARLO/BERR). */
  uint32_t sr1 = hi2c2.Instance->SR1;
  uint32_t sr2 = hi2c2.Instance->SR2;
  uint8_t sda_low = (GPIOH->IDR & GPIO_PIN_5) ? 0 : 1;
  uint8_t busy = (sr2 & I2C_SR2_BUSY) ? 1 : 0;
  uint8_t err  = (sr1 & (I2C_SR1_AF | I2C_SR1_ARLO | I2C_SR1_BERR)) ? 1 : 0;

  if (!sda_low && !busy && !err)
  {
    return 0;   /* bus looks healthy, nothing to do */
  }

  /* 2. Clock out up to 9 pulses on SCL to release a stuck slave.
   *    Resample SDA after each pulse; once it goes HIGH the slave has
   *    released the bus and we can stop early. */
  i2c_scl_gpio_out();
  HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4, GPIO_PIN_SET);   /* idle-high SCL */
  i2c_udelay(10);
  for (int i = 0; i < 9; i++)
  {
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4, GPIO_PIN_RESET);
    i2c_udelay(10);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4, GPIO_PIN_SET);
    i2c_udelay(10);
    if (GPIOH->IDR & GPIO_PIN_5) break;                 /* SDA released */
  }
  /* Generate a STOP condition: SDA goes LOW->HIGH while SCL is HIGH. */
  /* (SCL already high; drive SDA low then high to end the fake transaction.) */
  {
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_5; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOH, &g);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_5, GPIO_PIN_RESET);
    i2c_udelay(10);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_5, GPIO_PIN_SET);
    i2c_udelay(10);
  }

  /* 3. Reset the peripheral to clear any latched error / BUSY flag. */
  HAL_I2C_DeInit(&hi2c2);
  HAL_I2C_Init(&hi2c2);

  /* 4. Restore the pins to I2C AF open-drain and re-apply our timing config. */
  i2c_gpio_af_od();
  hi2c2.Init.ClockSpeed      = 100000;
  hi2c2.Init.DutyCycle       = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1     = 0;
  hi2c2.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2     = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    return -1;
  }

  /* 5. Verify the bus is no longer stuck. */
  if ((hi2c2.Instance->SR2 & I2C_SR2_BUSY) ||
      !(GPIOH->IDR & GPIO_PIN_5))
  {
    return -1;
  }
  return 0;
}
