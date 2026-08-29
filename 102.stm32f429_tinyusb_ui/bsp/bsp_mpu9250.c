/**
  ******************************************************************************
  * @file    bsp_mpu9250.c
  * @brief   MPU9250 9-axis IMU driver on I2C2 (PH4/PH5).
  *
  *          Accel/gyro are read directly from the MPU9250 register map.
  *          The AK8963 magnetometer is behind the MPU9250's I2C master
  *          (slave 0, addr 0x0C): we configure it once for continuous
  *          16-bit mode and read the samples from EXT_SENS_DATA.
  ******************************************************************************
  */
#include "bsp_mpu9250.h"
#include "bsp_i2c.h"
#include "bsp_delay.h"

#define MPU9250_ADDR        0x68U
#define AK8963_ADDR         0x0CU
/* See the comment on AP3216C_TIMEOUT: HAL_I2C_Mem_* is a polling transfer and
 * 10 ms was not enough headroom once the touch task (higher priority) started
 * bit-banging on the neighbouring pins.  50 ms rides out preemption; a timeout
 * here leaves the bus with SDA held low, so it must not happen spuriously. */
#define I2C_TIMEOUT         50U

/* MPU9250 registers */
#define MPU_SMPLRT_DIV      0x19U
#define MPU_CONFIG          0x1AU
#define MPU_GYRO_CONFIG     0x1BU
#define MPU_ACCEL_CONFIG    0x1CU
#define MPU_INT_PIN_CFG     0x37U
#define MPU_ACCEL_XOUT_H    0x3BU
#define MPU_GYRO_XOUT_H     0x43U
#define MPU_EXT_SENS_00     0x49U
#define MPU_USER_CTRL       0x6AU
#define MPU_PWR_MGMT_1      0x6BU
#define MPU_WHO_AM_I        0x75U
#define MPU_I2C_MST_CTRL    0x24U
#define MPU_I2C_SLV0_ADDR   0x25U
#define MPU_I2C_SLV0_REG    0x26U
#define MPU_I2C_SLV0_CTRL   0x27U
#define MPU_I2C_SLV0_DO     0x63U

/* AK8963 registers */
#define AK8963_WIA          0x00U   /* device ID, must read 0x48            */
#define AK8963_ST1          0x02U   /* status 1: bit0 = data ready          */
#define AK8963_HXL          0x03U
#define AK8963_CNTL         0x0AU
#define AK8963_ASAX         0x10U
#define AK8963_ST2          0x09U   /* status 2: bit3 = magnetic overflow   */

#define AK8963_WIA_VALUE    0x48U
#define AK8963_CNTL_CONT2   0x16U   /* continuous mode 2, 16-bit output */

static float   g_asa[3] = { 1.0f, 1.0f, 1.0f };
static uint8_t g_mag_id = 0U;       /* AK8963 WIA read during init          */

static int mpu_write(uint8_t reg, uint8_t val)
{
  return (HAL_I2C_Mem_Write(&hi2c2, MPU9250_ADDR << 1, reg, 1,
                            &val, 1, I2C_TIMEOUT) == HAL_OK) ? 0 : -1;
}

static int mpu_read(uint8_t reg, uint8_t *val)
{
  return (HAL_I2C_Mem_Read(&hi2c2, MPU9250_ADDR << 1, reg, 1,
                           val, 1, I2C_TIMEOUT) == HAL_OK) ? 0 : -1;
}

/* Timing (fixed 2026-08-29): slave 0 only transfers on an MPU sample, and with
 * SMPLRT_DIV = 7 the sample rate is 1 kHz / 8 = 125 Hz, i.e. one transfer every
 * 8 ms.  Waiting only 2 ms read EXT_SENS_DATA before the first transfer had
 * happened, so every magnetometer axis came back as exactly 0.0 uT while the
 * call still reported success - a silently wrong value, which is worse than a
 * failure.  MPU_SLAVE_SETTLE_MS covers two sample periods. */
#define MPU_SLAVE_SETTLE_MS  20U

/* Write one byte to the AK8963 through the MPU9250 I2C master (slave 0). */
static int ak8963_write(uint8_t reg, uint8_t val)
{
  if (mpu_write(MPU_I2C_SLV0_ADDR, AK8963_ADDR << 1) != 0) return -1;   /* write */
  if (mpu_write(MPU_I2C_SLV0_REG, reg) != 0) return -1;
  if (mpu_write(MPU_I2C_SLV0_DO, val) != 0) return -1;
  if (mpu_write(MPU_I2C_SLV0_CTRL, 0x81U) != 0) return -1;              /* 1 byte + enable */
  bsp_delay_ms(MPU_SLAVE_SETTLE_MS);
  return 0;
}

/* Read N bytes from the AK8963 via EXT_SENS_DATA (slave 0 must be set to
 * continuous-read those registers).  See MPU_SLAVE_SETTLE_MS for the timing. */
static int ak8963_read(uint8_t reg, uint8_t *buf, uint8_t n)
{
  if (mpu_write(MPU_I2C_SLV0_ADDR, (AK8963_ADDR << 1) | 1U) != 0) return -1; /* read */
  if (mpu_write(MPU_I2C_SLV0_REG, reg) != 0) return -1;
  if (mpu_write(MPU_I2C_SLV0_CTRL, (uint8_t)(0x80U | n)) != 0) return -1;    /* n bytes + enable */
  bsp_delay_ms(MPU_SLAVE_SETTLE_MS);

  for (uint8_t i = 0; i < n; i++)
  {
    if (mpu_read((uint8_t)(MPU_EXT_SENS_00 + i), &buf[i]) != 0) return -1;
  }
  return 0;
}

static int16_t rd16(const uint8_t *b)
{
  return (int16_t)(((uint16_t)b[0] << 8) | b[1]);
}

int bsp_mpu9250_init(void)
{
  uint8_t id = 0;

  if (mpu_read(MPU_WHO_AM_I, &id) != 0 || id != 0x71U)
  {
    return -1;
  }

  /* wake up, no reset, clock = PLL */
  mpu_write(MPU_PWR_MGMT_1, 0x00U);
  bsp_delay_ms(10);

  /* sample rate = gyro / (1+7), DLPF ~ 44Hz */
  mpu_write(MPU_SMPLRT_DIV, 0x07U);
  mpu_write(MPU_CONFIG, 0x03U);

  /* full scale: gyro +-2000 dps (0x18), accel +-8g (0x10) */
  mpu_write(MPU_GYRO_CONFIG, 0x18U);
  mpu_write(MPU_ACCEL_CONFIG, 0x10U);

  /* enable I2C master mode to reach the AK8963 (400 kHz) */
  mpu_write(MPU_USER_CTRL, 0x20U);
  mpu_write(MPU_I2C_MST_CTRL, 0x0DU);
  bsp_delay_ms(5);

  /* read the AK8963 sensitivity adjustment (ASX/ASY/ASZ) */
  {
    uint8_t asa[3];
    if (ak8963_read(AK8963_ASAX, asa, 3) == 0)
    {
      for (int i = 0; i < 3; i++)
      {
        g_asa[i] = ((float)(int)asa[i] - 128.0f) * 0.5f / 128.0f + 1.0f;
      }
    }
  }

  /* ---- probe the AK8963 before trusting it ----------------------------- *
   * Plenty of "MPU9250" modules either carry no magnetometer at all or have
   * one that does not answer on the internal I2C master.  WIA (0x00) must read
   * 0x48; without this check a dead magnetometer silently reports 0.0 uT for
   * every axis, which looks like a successful read. */
  g_mag_id = 0U;
  (void)ak8963_read(AK8963_WIA, &g_mag_id, 1);

  if (g_mag_id != AK8963_WIA_VALUE)
  {
    /* accel and gyro are fine; report the missing magnetometer as -3. */
    return -3;
  }

  /* AK8963: continuous measurement mode 2, 16-bit */
  if (ak8963_write(AK8963_CNTL, AK8963_CNTL_CONT2) != 0)
  {
    return -2;
  }

  return 0;
}

/**
  * @brief  AK8963 device ID read during init (0x48 when the magnetometer
  *         answers).  Lets the caller tell "no magnetometer fitted" apart from
  *         "magnetometer read failed".
  */
uint8_t bsp_mpu9250_mag_id(void)
{
  return g_mag_id;
}

int bsp_mpu9250_read(mpu9250_data_t *d)
{
  uint8_t buf[6];

  /* accel (0x3B, 6 bytes) */
  if (HAL_I2C_Mem_Read(&hi2c2, MPU9250_ADDR << 1, MPU_ACCEL_XOUT_H, 1,
                       buf, 6, I2C_TIMEOUT) != HAL_OK)
  {
    return -1;
  }
  d->ax = (float)rd16(buf) / 4096.0f;      /* +-8g */
  d->ay = (float)rd16(buf + 2) / 4096.0f;
  d->az = (float)rd16(buf + 4) / 4096.0f;

  /* gyro (0x43, 6 bytes) */
  if (HAL_I2C_Mem_Read(&hi2c2, MPU9250_ADDR << 1, MPU_GYRO_XOUT_H, 1,
                       buf, 6, I2C_TIMEOUT) != HAL_OK)
  {
    return -2;
  }
  d->gx = (float)rd16(buf) / 16.4f;        /* +-2000 dps */
  d->gy = (float)rd16(buf + 2) / 16.4f;
  d->gz = (float)rd16(buf + 4) / 16.4f;

  /* magnetometer via EXT_SENS_DATA (slave 0 already reads AK8963 HXL) */
  if (ak8963_read(AK8963_HXL, buf, 6) != 0)
  {
    return -3;
  }
  /* 0.15 uT/LSB at 16-bit, then apply the sensitivity adjustment */
  d->mx = (float)rd16(buf) * 0.15f * g_asa[0];
  d->my = (float)rd16(buf + 2) * 0.15f * g_asa[1];
  d->mz = (float)rd16(buf + 4) * 0.15f * g_asa[2];

  return 0;
}
