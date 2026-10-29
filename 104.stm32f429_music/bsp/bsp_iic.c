/**
  ******************************************************************************
  * @file    bsp_iic.c
  * @brief   Bit-banged I2C master for the WM8978 (PH4 = SCL, PH5 = SDA).
  *
  *          Pin access goes through BSRR/IDR (not HAL_GPIO_WritePin) because the
  *          bus timing is built from bsp_delay_us() and every extra call in the
  *          loop would shorten the low phase unpredictably.
  ******************************************************************************
  */
#include "bsp_iic.h"
#include "bsp_delay.h"

#include <stddef.h>

/* ---- pin helpers ------------------------------------------------------- */
#define SCL_HIGH()        (GPIOH->BSRR = (uint32_t)BSP_IIC_SCL_PIN)
#define SCL_LOW()         (GPIOH->BSRR = (uint32_t)BSP_IIC_SCL_PIN << 16)
#define SDA_HIGH()        (GPIOH->BSRR = (uint32_t)BSP_IIC_SDA_PIN)
#define SDA_LOW()         (GPIOH->BSRR = (uint32_t)BSP_IIC_SDA_PIN << 16)
#define SDA_READ()        (((GPIOH->IDR & BSP_IIC_SDA_PIN) != 0U) ? 1U : 0U)
#define SDA_IS_OUTPUT()   (GPIOH->MODER = (GPIOH->MODER & ~(3UL << (5U * 2U))) \
                                        | (1UL << (5U * 2U)))
#define SDA_IS_INPUT()    (GPIOH->MODER = (GPIOH->MODER & ~(3UL << (5U * 2U))))

#define IIC_DELAY()       bsp_delay_us(BSP_IIC_DELAY_US)

static void iic_start(void)
{
  SDA_IS_OUTPUT();
  SDA_HIGH();
  SCL_HIGH();
  IIC_DELAY();
  SDA_LOW();          /* SDA falls while SCL is high -> START */
  IIC_DELAY();
  SCL_LOW();
}

static void iic_stop(void)
{
  SDA_IS_OUTPUT();
  SCL_LOW();
  SDA_LOW();
  IIC_DELAY();
  SCL_HIGH();
  IIC_DELAY();
  SDA_HIGH();         /* SDA rises while SCL is high -> STOP */
  IIC_DELAY();
}

/* @retval 0 slave acknowledged, 1 no ACK */
static uint8_t iic_wait_ack(void)
{
  uint8_t waited = 0U;

  SDA_IS_INPUT();
  SDA_HIGH();
  IIC_DELAY();
  SCL_HIGH();
  IIC_DELAY();

  while (SDA_READ() != 0U)
  {
    if (++waited > 250U)
    {
      iic_stop();
      return 1U;
    }
  }
  SCL_LOW();
  return 0U;
}

static void iic_send_byte(uint8_t value)
{
  uint8_t i;

  SDA_IS_OUTPUT();
  for (i = 0U; i < 8U; i++)
  {
    if ((value & 0x80U) != 0U)
    {
      SDA_HIGH();
    }
    else
    {
      SDA_LOW();
    }
    value = (uint8_t)(value << 1);
    IIC_DELAY();
    SCL_HIGH();
    IIC_DELAY();
    SCL_LOW();
  }
  IIC_DELAY();
}

/* ---- public API -------------------------------------------------------- */
void bsp_iic_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOH_CLK_ENABLE();

  gpio.Pin = BSP_IIC_SCL_PIN | BSP_IIC_SDA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOH, &gpio);

  SDA_HIGH();
  SCL_HIGH();
}

bool bsp_iic_write_bytes(uint8_t dev_addr, const uint8_t *data, uint8_t len)
{
  uint8_t i;

  if ((data == NULL) || (len == 0U))
  {
    return false;
  }

  iic_start();
  iic_send_byte((uint8_t)(dev_addr << 1));
  if (iic_wait_ack() != 0U)
  {
    return false;
  }

  for (i = 0U; i < len; i++)
  {
    iic_send_byte(data[i]);
    if (iic_wait_ack() != 0U)
    {
      return false;
    }
  }

  iic_stop();
  return true;
}

bool bsp_iic_write_byte(uint8_t dev_addr, uint8_t value)
{
  return bsp_iic_write_bytes(dev_addr, &value, 1U);
}

uint8_t bsp_iic_read_byte(uint8_t dev_addr)
{
  uint8_t value = 0U;
  uint8_t i;

  iic_start();
  iic_send_byte((uint8_t)((dev_addr << 1) | 1U));
  if (iic_wait_ack() != 0U)
  {
    return 0U;
  }

  SDA_IS_INPUT();
  for (i = 0U; i < 8U; i++)
  {
    value = (uint8_t)(value << 1);
    IIC_DELAY();
    SCL_HIGH();
    IIC_DELAY();
    if (SDA_READ() != 0U)
    {
      value |= 1U;
    }
    SCL_LOW();
  }

  /* NACK the last byte, then release the bus. */
  SDA_IS_OUTPUT();
  SDA_HIGH();
  IIC_DELAY();
  SCL_HIGH();
  IIC_DELAY();
  SCL_LOW();
  iic_stop();

  return value;
}
