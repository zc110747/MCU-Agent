/**
  ******************************************************************************
  * @file    bsp_gt9147.c
  * @brief   GT9147 capacitive touch controller driver (software I2C).
  *
  *  Bring-up sequence (from the GT9147 programming guide):
  *
  *    1. drive RST low for >1 ms, then release it
  *    2. leave INT as a floating input - the chip latches its slave address
  *       from INT while RST is released (0x14 when INT reads high)
  *    3. wait for the chip to boot, then read the 4-byte Product ID at 0x8140
  *    4. when the ID is "9147": soft reset, read the configuration version at
  *       0x8047 and, if it is older than the block shipped here, upload the
  *       new block together with its checksum + refresh flag at 0x80FF
  *
  *  Every step prints what it saw, so a wrong ID, a wrong address or a stuck
  *  bus is visible on the debug console without a logic analyser.
  ******************************************************************************
  */
#include "bsp_gt9147.h"
#include "bsp_sw_i2c.h"
#include "bsp_delay.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include "log.h"

/* ---- Pin assignment ------------------------------------------------------- */
#define GT_RST_PORT         GPIOI
#define GT_RST_PIN          GPIO_PIN_8      /* T_CS  -> CT_RST */
#define GT_INT_PORT         GPIOH
#define GT_INT_PIN          GPIO_PIN_7      /* T_PEN -> CT_INT */

/* ---- I2C addresses (7-bit, unshifted) ------------------------------------- */
#define GT_ADDR_LO          0x14U           /* INT high during reset */
#define GT_ADDR_HI          0x5DU           /* INT low  during reset */

/* ---- Register map --------------------------------------------------------- */
#define GT_CTRL_REG         0x8040U         /* control / soft reset           */
#define GT_CFGS_REG         0x8047U         /* configuration block start      */
#define GT_CHECK_REG        0x80FFU         /* checksum + refresh flag        */
#define GT_PID_REG          0x8140U         /* product ID (4 ASCII chars)     */
#define GT_GSTID_REG        0x814EU         /* touch status / point count     */
#define GT_TP1_REG          0x8150U         /* point 1 (8 bytes per point)    */

#define GT_TP_STRIDE        8U              /* bytes between consecutive pts  */

/* Bit 7 of the status register: the coordinate buffer has been updated. */
#define GT_BUFFER_READY     0x80U

/* Number of bytes in the configuration block (0x8047 .. 0x80FE). */
#define GT_CFG_LEN          184U

/* Version of the configuration block below.  The chip only accepts a block
 * whose version is >= the one it has stored, which is why the table starts
 * with a high version byte. */
#define GT_CFG_VERSION      0x60U

/**
  * Configuration block for the 800x480 NT35510 module wired to this board.
  * Byte 0 is the version, bytes 1..4 are the touch resolution (480 x 800,
  * little endian); the remaining bytes are the panel-specific sensing and
  * reporting parameters.
  */
static const uint8_t GT_CFG_TBL[GT_CFG_LEN] =
{
    0x60, 0xE0, 0x01, 0x20, 0x03, 0x05, 0x35, 0x00, 0x02, 0x08,
    0x1E, 0x08, 0x50, 0x3C, 0x0F, 0x05, 0x00, 0x00, 0xFF, 0x67,
    0x50, 0x00, 0x00, 0x18, 0x1A, 0x1E, 0x14, 0x89, 0x28, 0x0A,
    0x30, 0x2E, 0xBB, 0x0A, 0x03, 0x00, 0x00, 0x02, 0x33, 0x1D,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00,
    0x2A, 0x1C, 0x5A, 0x94, 0xC5, 0x02, 0x07, 0x00, 0x00, 0x00,
    0xB5, 0x1F, 0x00, 0x90, 0x28, 0x00, 0x77, 0x32, 0x00, 0x62,
    0x3F, 0x00, 0x52, 0x50, 0x00, 0x52, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F,
    0x0F, 0x03, 0x06, 0x10, 0x42, 0xF8, 0x0F, 0x14, 0x00, 0x00,
    0x00, 0x00, 0x1A, 0x18, 0x16, 0x14, 0x12, 0x10, 0x0E, 0x0C,
    0x0A, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x29, 0x28, 0x24, 0x22, 0x20, 0x1F, 0x1E, 0x1D,
    0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x05, 0x04, 0x02, 0x00, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
};

static uint8_t  s_addr   = 0U;      /* 7-bit address the chip answered on   */
static uint8_t  s_ready  = 0U;      /* 1 once init completed successfully   */
static uint8_t  s_ver    = 0U;      /* configuration version in the chip    */
static uint16_t s_panel_x = 0U;     /* touch resolution, from the config    */
static uint16_t s_panel_y = 0U;
static char     s_id[GT9147_ID_LEN + 1U] = "?";

/* -------------------------------------------------------------------------- */
/* Bus transactions                                                           */
/* -------------------------------------------------------------------------- */
static int gt_write(uint16_t reg, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    bsp_sw_i2c_start();
    if (bsp_sw_i2c_send_byte((uint8_t)(s_addr << 1)) != 0) { goto fail; }
    if (bsp_sw_i2c_send_byte((uint8_t)(reg >> 8)) != 0)    { goto fail; }
    if (bsp_sw_i2c_send_byte((uint8_t)(reg & 0xFFU)) != 0) { goto fail; }
    for (i = 0U; i < len; i++)
    {
        if (bsp_sw_i2c_send_byte(buf[i]) != 0) { goto fail; }
    }
    bsp_sw_i2c_stop();
    return 0;

fail:
    bsp_sw_i2c_stop();
    return -1;
}

static int gt_read(uint16_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    bsp_sw_i2c_start();
    if (bsp_sw_i2c_send_byte((uint8_t)(s_addr << 1)) != 0) { goto fail; }
    if (bsp_sw_i2c_send_byte((uint8_t)(reg >> 8)) != 0)    { goto fail; }
    if (bsp_sw_i2c_send_byte((uint8_t)(reg & 0xFFU)) != 0) { goto fail; }

    bsp_sw_i2c_start();                                     /* repeated start */
    if (bsp_sw_i2c_send_byte((uint8_t)((s_addr << 1) | 1U)) != 0) { goto fail; }
    for (i = 0U; i < len; i++)
    {
        buf[i] = bsp_sw_i2c_read_byte((i == (len - 1U)) ? 0 : 1);
    }
    bsp_sw_i2c_stop();
    return 0;

fail:
    bsp_sw_i2c_stop();
    return -1;
}

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

/**
  * @brief  Upload the configuration block and ask the chip to store it.
  * @param  save 1 to store the block in the chip's flash (survives power down).
  */
static int gt_send_cfg(int save)
{
    uint8_t buf[2];
    uint32_t sum = 0U;
    uint16_t i;

    for (i = 0U; i < GT_CFG_LEN; i++)
    {
        sum += GT_CFG_TBL[i];
    }
    buf[0] = (uint8_t)((~sum) + 1U);        /* two's complement checksum */
    buf[1] = (uint8_t)(save ? 1U : 0U);     /* 1 = write to flash        */

    if (gt_write(GT_CFGS_REG, GT_CFG_TBL, GT_CFG_LEN) != 0) { return -1; }
    if (gt_write(GT_CHECK_REG, buf, 2U) != 0)               { return -1; }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
int bsp_gt9147_init(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t id[GT9147_ID_LEN];
    uint8_t tmp;
    static const uint8_t addr_candidates[2] = { GT_ADDR_LO, GT_ADDR_HI };
    int i;

    s_ready = 0U;
    s_addr  = 0U;
    s_ver   = 0U;
    (void)memset(s_id, 0, sizeof(s_id));
    s_id[0] = '?';

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    /* RST is a push-pull output; INT is an input - during the reset pulse the
     * chip samples it to pick its slave address, so it must never be driven. */
    gpio.Mode      = GPIO_MODE_OUTPUT_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = 0U;
    gpio.Pin       = GT_RST_PIN;
    HAL_GPIO_Init(GT_RST_PORT, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = GT_INT_PIN;
    HAL_GPIO_Init(GT_INT_PORT, &gpio);

    bsp_sw_i2c_init();

    /* ---- reset pulse ---------------------------------------------------- */
    HAL_GPIO_WritePin(GT_RST_PORT, GT_RST_PIN, GPIO_PIN_RESET);
    bsp_delay_ms(10U);
    HAL_GPIO_WritePin(GT_RST_PORT, GT_RST_PIN, GPIO_PIN_SET);
    bsp_delay_ms(10U);

    /* Float INT: the address latch is sampled from it, and after boot it
     * becomes the interrupt output. */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin  = GT_INT_PIN;
    HAL_GPIO_Init(GT_INT_PORT, &gpio);
    bsp_delay_ms(100U);

    /* ---- re-enable the pull-up on INT ------------------------------------ *
     * NOPULL above is only correct for the address latch (the pin must read
     * HIGH to select 0x14).  From here on INT drives EXTI line 7, and a
     * floating interrupt pin is a noise antenna:
     *
     *   - it picks up ambient noise and generates spurious edges;
     *   - worse, PH6 is the bit-banged SCL toggling at ~165 kHz and it is the
     *     adjacent pin, so capacitive crosstalk injects edges straight into
     *     PH7.  Measured on this board: ~15 kHz of phantom interrupts (the
     *     irq counter climbed past 93 000 within seconds), which starved the
     *     I2C2 sensor task and ended up locking its bus.
     *
     * The controller idles INT high and pulls it low to report, so a pull-up
     * is the correct termination for the interrupt use. */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = GT_INT_PIN;
    HAL_GPIO_Init(GT_INT_PORT, &gpio);
    bsp_delay_ms(2U);

    /* ---- find the chip and read its product ID -------------------------- */
    for (i = 0; i < 2; i++)
    {
        s_addr = addr_candidates[i];
        (void)memset(id, 0, sizeof(id));
        if (gt_read(GT_PID_REG, id, GT9147_ID_LEN) == 0)
        {
            break;
        }
        s_addr = 0U;
    }

    (void)memcpy(s_id, id, GT9147_ID_LEN);
    s_id[GT9147_ID_LEN] = '\0';

    if (s_addr == 0U)
    {
        PRINT_LOG("[TOUCH] GT9147 not found on 0x%02X / 0x%02X (check T_SCK/T_MOSI wiring)\r\n",
               (unsigned int)GT_ADDR_LO, (unsigned int)GT_ADDR_HI);
        return -1;
    }

    /* ---- verify the product ID ------------------------------------------ */
    {
        int known = (strcmp(s_id, "9147") == 0) ||
                    (strcmp(s_id, "911")  == 0) ||
                    (strcmp(s_id, "1158") == 0) ||
                    (strcmp(s_id, "9271") == 0) ||
                    (strcmp(s_id, "928")  == 0);

        PRINT_LOG("[TOUCH] product ID = \"%s\" (addr 0x%02X) -> %s\r\n",
               s_id, (unsigned int)s_addr, known ? "MATCH" : "MISMATCH");

        if (!known)
        {
            PRINT_LOG("[TOUCH] expected one of 911 / 9147 / 1158 / 9271 / 928\r\n");
            return -2;
        }
    }

    /* ---- report what the chip is configured for ------------------------ *
     * Read on every supported part, not just the 9147: the resolution is
     * what the raw coordinates are expressed in, so the caller needs it to
     * map a touch onto the display.  (Only the 9147 gets the configuration
     * block below uploaded - the block is 9147 specific.) */
    {
        uint8_t cfg[5];

        if (gt_read(GT_CFGS_REG, cfg, sizeof(cfg)) == 0)
        {
            s_ver     = cfg[0];
            s_panel_x = (uint16_t)(((uint16_t)cfg[2] << 8) | cfg[1]);
            s_panel_y = (uint16_t)(((uint16_t)cfg[4] << 8) | cfg[3]);
        }
        PRINT_LOG("[TOUCH] stored config: version=0x%02X resolution=%ux%u\r\n",
               (unsigned int)s_ver, (unsigned int)s_panel_x, (unsigned int)s_panel_y);
    }

    /* ---- GT9147: refresh the configuration if it is out of date --------- */
    if (strcmp(s_id, "9147") == 0)
    {
        /* soft reset: hold the chip in reset while the block is uploaded */
        tmp = 0x02U;
        if (gt_write(GT_CTRL_REG, &tmp, 1U) != 0)
        {
            PRINT_LOG("[TOUCH] soft reset write FAILED\r\n");
            return -2;
        }

        if (s_ver < GT_CFG_VERSION)
        {
            if (gt_send_cfg(1) != 0)
            {
                PRINT_LOG("[TOUCH] config upload FAILED\r\n");
            }
            else
            {
                PRINT_LOG("[TOUCH] config updated (%u bytes, %ux%u) and saved\r\n",
                       (unsigned int)GT_CFG_LEN,
                       (unsigned int)GT9147_PANEL_X, (unsigned int)GT9147_PANEL_Y);
                s_ver     = GT_CFG_VERSION;
                s_panel_x = GT9147_PANEL_X;
                s_panel_y = GT9147_PANEL_Y;
            }
        }
        else
        {
            PRINT_LOG("[TOUCH] config already up to date, kept as is\r\n");
        }

        /* release the soft reset */
        tmp = 0x00U;
        (void)gt_write(GT_CTRL_REG, &tmp, 1U);
        bsp_delay_ms(10U);
    }

    s_ready = 1U;
    return 0;
}

int bsp_gt9147_is_ready(void)
{
    return (s_ready != 0U) ? 1 : 0;
}

const char *bsp_gt9147_id(void)
{
    return s_id;
}

uint8_t bsp_gt9147_addr(void)
{
    return s_addr;
}

uint8_t bsp_gt9147_cfg_version(void)
{
    return s_ver;
}

uint16_t bsp_gt9147_panel_x(void)
{
    return s_panel_x;
}

uint16_t bsp_gt9147_panel_y(void)
{
    return s_panel_y;
}

int bsp_gt9147_read(gt9147_point_t *points, uint8_t max)
{
    uint8_t status = 0U;
    uint8_t count;
    uint8_t buf[4];
    uint8_t i;
    uint8_t zero = 0U;

    if ((s_ready == 0U) || (points == NULL) || (max == 0U))
    {
        return -1;
    }

    if (gt_read(GT_GSTID_REG, &status, 1U) != 0)
    {
        return -1;
    }

    count = (uint8_t)(status & 0x0FU);
    if (count > GT9147_MAX_POINTS)
    {
        count = GT9147_MAX_POINTS;
    }
    if (count > max)
    {
        count = max;
    }

    if (count != 0U)
    {
        for (i = 0U; i < count; i++)
        {
            if (gt_read((uint16_t)(GT_TP1_REG + (i * GT_TP_STRIDE)), buf, 4U) != 0)
            {
                break;
            }
            points[i].x = (uint16_t)(((uint16_t)buf[1] << 8) | buf[0]);
            points[i].y = (uint16_t)(((uint16_t)buf[3] << 8) | buf[2]);
        }
        count = i;
    }

    /* Clear the status register: the chip only raises the next interrupt once
     * this has been written back to 0. */
    (void)gt_write(GT_GSTID_REG, &zero, 1U);

    return (int)count;
}
