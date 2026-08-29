/**
  ******************************************************************************
  * @file    bsp_sw_i2c.c
  * @brief   Software (bit-bang) I2C master for the GT9147 touch controller.
  *
  *  Both lines are open-drain with a pull-up, so "drive high" simply means
  *  "release the line".  That removes all direction switching: the input data
  *  register stays valid while the pin is configured as an open-drain output.
  *
  *  Timing is a DWT cycle-count busy wait (bsp_delay_us), not HAL_Delay:
  *  the touch chip is brought up inside a FreeRTOS task where the TIM7 tick
  *  may be masked by BASEPRI, and a 1 ms granularity would be far too coarse
  *  for a 165 kHz clock anyway.
  ******************************************************************************
  */
#include "bsp_sw_i2c.h"
#include "bsp_delay.h"
#include "stm32f4xx_hal.h"

/* ---- Pin assignment (see bsp_sw_i2c.h for the wiring rationale) ----------- */
#define SWI2C_SCL_PORT      GPIOH
#define SWI2C_SCL_PIN       GPIO_PIN_6      /* T_SCK  -> CT_SCL */
#define SWI2C_SDA_PORT      GPIOI
#define SWI2C_SDA_PIN       GPIO_PIN_3      /* T_MOSI -> CT_SDA */

static void scl_high(void)
{
    HAL_GPIO_WritePin(SWI2C_SCL_PORT, SWI2C_SCL_PIN, GPIO_PIN_SET);
    bsp_delay_us(SWI2C_HALF_PERIOD_US);
}

static void scl_low(void)
{
    HAL_GPIO_WritePin(SWI2C_SCL_PORT, SWI2C_SCL_PIN, GPIO_PIN_RESET);
    bsp_delay_us(SWI2C_HALF_PERIOD_US);
}

static void sda_high(void)
{
    HAL_GPIO_WritePin(SWI2C_SDA_PORT, SWI2C_SDA_PIN, GPIO_PIN_SET);
    bsp_delay_us(SWI2C_HALF_PERIOD_US / 2U);
}

static void sda_low(void)
{
    HAL_GPIO_WritePin(SWI2C_SDA_PORT, SWI2C_SDA_PIN, GPIO_PIN_RESET);
    bsp_delay_us(SWI2C_HALF_PERIOD_US / 2U);
}

static int sda_read(void)
{
    return (HAL_GPIO_ReadPin(SWI2C_SDA_PORT, SWI2C_SDA_PIN) == GPIO_PIN_SET) ? 1 : 0;
}

void bsp_sw_i2c_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio.Mode  = GPIO_MODE_OUTPUT_OD;   /* open drain: SET == released */
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = 0U;

    gpio.Pin = SWI2C_SCL_PIN;
    HAL_GPIO_Init(SWI2C_SCL_PORT, &gpio);

    gpio.Pin = SWI2C_SDA_PIN;
    HAL_GPIO_Init(SWI2C_SDA_PORT, &gpio);

    /* Park the bus in the idle state: both lines released. */
    HAL_GPIO_WritePin(SWI2C_SCL_PORT, SWI2C_SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SWI2C_SDA_PORT, SWI2C_SDA_PIN, GPIO_PIN_SET);
    bsp_delay_us(20U);
}

void bsp_sw_i2c_start(void)
{
    sda_high();
    scl_high();
    sda_low();          /* SDA falls while SCL is high -> START */
    scl_low();
}

void bsp_sw_i2c_stop(void)
{
    scl_low();
    sda_low();
    scl_high();
    sda_high();         /* SDA rises while SCL is high -> STOP */
}

int bsp_sw_i2c_send_byte(uint8_t data)
{
    uint8_t i;
    int ack;

    for (i = 0U; i < 8U; i++)
    {
        scl_low();
        if ((data & 0x80U) != 0U) { sda_high(); } else { sda_low(); }
        scl_high();                 /* slave samples on the rising edge */
        data = (uint8_t)(data << 1);
    }

    /* 9th clock: the slave pulls SDA low to acknowledge. */
    scl_low();
    sda_high();                     /* release SDA so the slave can drive it */
    scl_high();
    ack = sda_read();
    scl_low();

    return (ack == 0) ? 0 : -1;     /* SDA low == ACK */
}

uint8_t bsp_sw_i2c_read_byte(int ack)
{
    uint8_t i;
    uint8_t data = 0U;

    scl_low();
    sda_high();                     /* make sure we do not hold the line low */

    for (i = 0U; i < 8U; i++)
    {
        scl_low();
        bsp_delay_us(SWI2C_HALF_PERIOD_US);
        scl_high();
        data = (uint8_t)(data << 1);
        if (sda_read() != 0) { data |= 0x01U; }
    }

    /* 9th clock: master drives the ACK/NACK level itself. */
    scl_low();
    if (ack != 0) { sda_low(); } else { sda_high(); }
    scl_high();
    scl_low();
    sda_high();

    return data;
}

int bsp_sw_i2c_probe(uint8_t addr7)
{
    int ret;

    bsp_sw_i2c_start();
    ret = bsp_sw_i2c_send_byte((uint8_t)(addr7 << 1));   /* write direction */
    bsp_sw_i2c_stop();

    return ret;
}

int bsp_sw_i2c_bus_reset(void)
{
    uint32_t i;

    if (sda_read() != 0)
    {
        return 0;                   /* bus is free, nothing to do */
    }

    /* Clock the slave out of whatever byte it is stuck on. */
    for (i = 0U; i < SWI2C_RESET_PULSES; i++)
    {
        scl_low();
        scl_high();
        if (sda_read() != 0)
        {
            break;
        }
    }

    /* Finish with a STOP so the bus is left in a defined state. */
    bsp_sw_i2c_stop();

    return (sda_read() != 0) ? 0 : -1;
}
