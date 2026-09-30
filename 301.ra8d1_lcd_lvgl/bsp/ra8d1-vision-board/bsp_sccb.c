/**
 * @file bsp_sccb.c
 * @brief Bit-bang SCCB/I2C master (P1103 SCL / P50E SDA)
 *
 * Open-drain emulation:
 *   - SCL is master-only: plain push-pull output through bsp_pin_write().
 *   - SDA toggles between "drive low" (output, PODR=0) and "release"
 *     (input + pull-up) so the slave can ACK / drive read data.
 *
 * Half-bit is ~5 us (~100 kHz, the official OMV_I2C_SPEED_STANDARD),
 * generated with R_BSP_SoftwareDelay (cycle-accurate). 100 kHz keeps the
 * open-drain rise time (10K pull-up) comfortably inside the spec.
 */
#include "bsp_sccb.h"

#include "bsp_pin.h"
#include "r_ioport.h"

#define SCCB_HALF_BIT_US    (5U)

/* SDA never driven high: high = released, pulled up by the resistor/PFS. */
static void sda_release (void)
{
    (void) bsp_pin_cfg(BSP_SCCB_SDA_PIN,
                       (uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT |
                       (uint32_t) IOPORT_CFG_PULLUP_ENABLE);
}

static void sda_low (void)
{
    (void) bsp_pin_cfg(BSP_SCCB_SDA_PIN,
                       (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                       (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW);
}

static void sda_write (bool bit)
{
    if (bit)
    {
        sda_release();
    }
    else
    {
        sda_low();
    }
}

static bool sda_read (void)
{
    bsp_io_level_t level = BSP_IO_LEVEL_LOW;

    (void) bsp_pin_read(BSP_SCCB_SDA_PIN, &level);
    return (BSP_IO_LEVEL_HIGH == level);
}

static void scl_write (bool bit)
{
    (void) bsp_pin_write(BSP_SCCB_SCL_PIN,
                         bit ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

static void half_bit (void)
{
    R_BSP_SoftwareDelay(SCCB_HALF_BIT_US, BSP_DELAY_UNITS_MICROSECONDS);
}

static void bus_start (void)
{
    sda_release();
    scl_write(true);
    half_bit();
    sda_low();                   /* SDA falls while SCL is high */
    half_bit();
    scl_write(false);
    half_bit();
}

static void bus_stop (void)
{
    sda_low();
    half_bit();
    scl_write(true);
    half_bit();
    sda_release();               /* SDA rises while SCL is high */
    half_bit();
}

/** Clock out one bit. */
static void bus_write_bit (bool bit)
{
    sda_write(bit);
    half_bit();
    scl_write(true);
    half_bit();
    scl_write(false);
}

/** Clock in one bit (SDA released, slave drives). */
static bool bus_read_bit (void)
{
    bool bit;

    sda_release();
    half_bit();
    scl_write(true);
    half_bit();
    bit = sda_read();
    scl_write(false);
    return bit;
}

/** Send one byte, return true if the slave ACKed (SDA low on 9th clock). */
static bool bus_write_byte (uint8_t byte)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        bus_write_bit((byte & 0x80U) != 0U);
        byte = (uint8_t) (byte << 1);
    }

    return !bus_read_bit();      /* ACK = SDA low */
}

/** Receive one byte, then clock out the 9th bit: nack=true releases SDA
 *  (NACK, ends the read), nack=false drives SDA low (ACK, read continues). */
static uint8_t bus_read_byte (bool nack)
{
    uint8_t byte = 0U;

    for (uint8_t i = 0U; i < 8U; i++)
    {
        byte = (uint8_t) ((byte << 1) | (bus_read_bit() ? 1U : 0U));
    }

    bus_write_bit(nack);
    return byte;
}

void bsp_sccb_init (void)
{
    /* SCL push-pull output high; SDA released (input + pull-up). */
    (void) bsp_pin_cfg(BSP_SCCB_SCL_PIN,
                       (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                       (uint32_t) IOPORT_CFG_PORT_OUTPUT_HIGH);
    sda_release();
    half_bit();
}

bool bsp_sccb_probe (uint8_t addr7)
{
    bool acked;

    bus_start();
    acked = bus_write_byte((uint8_t) (addr7 << 1));  /* write phase, R/W=0 */
    bus_stop();
    return acked;
}

fsp_err_t bsp_sccb_write16 (uint8_t addr7, uint16_t reg, uint8_t val)
{
    fsp_err_t err = FSP_SUCCESS;

    bus_start();
    if (!bus_write_byte((uint8_t) (addr7 << 1)) ||
        !bus_write_byte((uint8_t) (reg >> 8)) ||
        !bus_write_byte((uint8_t) (reg & 0xFFU)) ||
        !bus_write_byte(val))
    {
        err = FSP_ERR_TRANSFER_ABORTED;
    }

    bus_stop();
    return err;
}

fsp_err_t bsp_sccb_read16 (uint8_t addr7, uint16_t reg, uint8_t * p_val)
{
    fsp_err_t err = FSP_SUCCESS;

    /* Dummy-write the register address... */
    bus_start();
    if (!bus_write_byte((uint8_t) (addr7 << 1)) ||
        !bus_write_byte((uint8_t) (reg >> 8)) ||
        !bus_write_byte((uint8_t) (reg & 0xFFU)))
    {
        err = FSP_ERR_TRANSFER_ABORTED;
    }

    if (FSP_SUCCESS == err)
    {
        /* ...then repeated-start and read one byte (NACK on the last). */
        bus_start();
        if (!bus_write_byte((uint8_t) ((addr7 << 1) | 1U)))
        {
            err = FSP_ERR_TRANSFER_ABORTED;
        }
        else
        {
            *p_val = bus_read_byte(true);   /* NACK: single-byte read */
        }
    }

    bus_stop();
    return err;
}

fsp_err_t bsp_sccb_write8 (uint8_t addr7, uint8_t reg, uint8_t val)
{
    fsp_err_t err = FSP_SUCCESS;

    bus_start();
    if (!bus_write_byte((uint8_t) (addr7 << 1)) ||
        !bus_write_byte(reg) ||
        !bus_write_byte(val))
    {
        err = FSP_ERR_TRANSFER_ABORTED;
    }

    bus_stop();
    return err;
}

fsp_err_t bsp_sccb_read8 (uint8_t addr7, uint8_t reg, uint8_t * p_val)
{
    fsp_err_t err = FSP_SUCCESS;

    bus_start();
    if (!bus_write_byte((uint8_t) (addr7 << 1)) ||
        !bus_write_byte(reg))
    {
        err = FSP_ERR_TRANSFER_ABORTED;
    }

    if (FSP_SUCCESS == err)
    {
        bus_start();
        if (!bus_write_byte((uint8_t) ((addr7 << 1) | 1U)))
        {
            err = FSP_ERR_TRANSFER_ABORTED;
        }
        else
        {
            *p_val = bus_read_byte(true);   /* NACK: single-byte read */
        }
    }

    bus_stop();
    return err;
}
