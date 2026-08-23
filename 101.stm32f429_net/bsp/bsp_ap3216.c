/**
  ******************************************************************************
  * @file    bsp_ap3216.c
  * @brief   AP3216C driver on I2C2 (PH4/PH5), ported from drv_i2c_ap3216.c.
  ******************************************************************************
  */
#include "bsp_ap3216.h"
#include "bsp_i2c.h"
#include "bsp_delay.h"

#define AP3216C_ADDR       0x1EU
#define AP3216C_TIMEOUT    10U

#define AP3216C_SYSTEMCONG 0x00U
#define AP3216C_IRDATALOW  0x0AU
#define AP3216C_IRDATAHIGH 0x0BU
#define AP3216C_ALSDATALOW 0x0CU
#define AP3216C_ALSDATAHIGH 0x0DU
#define AP3216C_PSDATALOW  0x0EU
#define AP3216C_PSDATAHIGH 0x0FU

static int ap3216_write_reg(uint8_t reg, uint8_t val)
{
  return (HAL_I2C_Mem_Write(&hi2c2, AP3216C_ADDR << 1, reg, 1,
                            &val, 1, AP3216C_TIMEOUT) == HAL_OK) ? 0 : -1;
}

static int ap3216_read_reg(uint8_t reg, uint8_t *val)
{
  return (HAL_I2C_Mem_Read(&hi2c2, AP3216C_ADDR << 1, reg, 1,
                           val, 1, AP3216C_TIMEOUT) == HAL_OK) ? 0 : -1;
}

int bsp_ap3216c_init(void)
{
  bsp_delay_ms(10);

  /* reset the sensor, then configure ALS+PS+IR */
  if (ap3216_write_reg(AP3216C_SYSTEMCONG, 0x04) != 0) return -1;
  bsp_delay_ms(20);
  if (ap3216_write_reg(AP3216C_SYSTEMCONG, 0x03) != 0) return -1;

  return 0;
}

int bsp_ap3216c_read(ap3216c_data_t *d)
{
  uint8_t buf[6];

  for (int i = 0; i < 6; i++)
  {
    if (ap3216_read_reg((uint8_t)(AP3216C_IRDATALOW + i), &buf[i]) != 0)
    {
      return -1;
    }
  }

  /* IR: bit15 = invalid */
  if (buf[0] & 0x80U)
  {
    d->ir = 0;
  }
  else
  {
    d->ir = (uint16_t)(((uint16_t)buf[1] << 2) | (buf[0] & 0x03U));
  }

  /* ALS */
  d->lux = (uint16_t)(((uint16_t)buf[3] << 8) | buf[2]);

  /* PS: bit14 = invalid */
  if (buf[4] & 0x40U)
  {
    d->ps = 0;
  }
  else
  {
    d->ps = (uint16_t)(((uint16_t)(buf[5] & 0x3FU) << 4) | (buf[4] & 0x0FU));
  }

  return 0;
}
