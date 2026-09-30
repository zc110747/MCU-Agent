/**
 * @file bsp_pin.c
 * @brief Board pin mux table for the RA8D1 Vision Board
 *
 * Source: official BSP ra_gen/pin_data.c (projects/lcd/vision_board_rgb_4.3inch).
 * Only the pins this project actually drives are listed, with the exact
 * pin_cfg value the generator produced (drive strength included, it matters:
 * the RGB bus and the SDRAM bus are configured DRIVE_HIGH / DRIVE_HS_HIGH).
 *
 * WARNING - the NMOS trap: IOPORT_CFG_NMOS_ENABLE (0x40) is N-channel open
 * drain. The generator only uses it on P408/P409. Copying it onto any other
 * pin silently kills the output (that is how P208 TXD died in Phase 1).
 */
#include "bsp_pin.h"

#include "r_ioport.h"

/* ---- helpers: peripheral pin, generator style --------------------------
   NOTE: the parameter is _pin, not pin - a parameter named "pin" would also
   replace the ".pin" designator and produce ".BSP_IO_PORT_02_PIN_08". */
#define PIN_PERIPH(_pin, _periph)                          \
    {                                                      \
        .pin     = (_pin),                                 \
        .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN | \
                    (uint32_t) (_periph)),                 \
    }

#define PIN_PERIPH_HI(_pin, _periph)                       \
    {                                                      \
        .pin     = (_pin),                                 \
        .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_HIGH |     \
                    (uint32_t) IOPORT_CFG_PERIPHERAL_PIN | \
                    (uint32_t) (_periph)),                 \
    }

#define PIN_PERIPH_MID(_pin, _periph)                      \
    {                                                      \
        .pin     = (_pin),                                 \
        .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_MID |      \
                    (uint32_t) IOPORT_CFG_PERIPHERAL_PIN | \
                    (uint32_t) (_periph)),                 \
    }

#define PIN_GPIO_OUT(_pin, _level)                                       \
    {                                                                    \
        .pin     = (_pin),                                               \
        .pin_cfg = ((uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |        \
                    ((_level) ? (uint32_t) IOPORT_CFG_PORT_OUTPUT_HIGH : \
                     (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW)),            \
    }

static ioport_instance_ctrl_t g_ioport_ctrl;

static const ioport_pin_cfg_t g_bsp_pin_cfg_data[] =
{
    /* ---- Phase 1: debug console (SCI9) + LED ---------------------------- */
    PIN_PERIPH(BSP_IO_PORT_02_PIN_08, IOPORT_PERIPHERAL_SCI1_3_5_7_9),  /* TXD9 */
    PIN_PERIPH(BSP_IO_PORT_02_PIN_09, IOPORT_PERIPHERAL_SCI1_3_5_7_9),  /* RXD9 */
    PIN_GPIO_OUT(BSP_IO_PORT_01_PIN_02, 0),                             /* LED */

    /* ---- Phase 2: GLCDC (RGB parallel interface + LCD clock) ----------- */
    PIN_PERIPH_MID(BSP_IO_PORT_02_PIN_07, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH(BSP_IO_PORT_05_PIN_15, IOPORT_PERIPHERAL_LCD_GRAPHICS),

    PIN_PERIPH_MID(BSP_IO_PORT_07_PIN_11, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_07_PIN_12, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_07_PIN_13, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_07_PIN_14, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_07_PIN_15, IOPORT_PERIPHERAL_LCD_GRAPHICS),

    PIN_PERIPH(BSP_IO_PORT_08_PIN_05, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_HI(BSP_IO_PORT_08_PIN_06, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH(BSP_IO_PORT_08_PIN_07, IOPORT_PERIPHERAL_LCD_GRAPHICS),

    PIN_PERIPH_MID(BSP_IO_PORT_09_PIN_02, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_09_PIN_03, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_09_PIN_04, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_09_PIN_10, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_09_PIN_11, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_09_PIN_12, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_09_PIN_13, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_09_PIN_14, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_09_PIN_15, IOPORT_PERIPHERAL_LCD_GRAPHICS),

    PIN_PERIPH_MID(BSP_IO_PORT_11_PIN_05, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_11_PIN_06, IOPORT_PERIPHERAL_LCD_GRAPHICS),
    PIN_PERIPH_MID(BSP_IO_PORT_11_PIN_07, IOPORT_PERIPHERAL_LCD_GRAPHICS),

    /* ---- Phase 2: SDRAM / external bus (DRIVE_HIGH, from generator) ---- */
    PIN_PERIPH_HI(BSP_IO_PORT_01_PIN_12, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_01_PIN_13, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_01_PIN_14, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_01_PIN_15, IOPORT_PERIPHERAL_BUS),

    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_00, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_01, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_02, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_03, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_04, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_05, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_06, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_07, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_08, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_09, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_10, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_11, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_03_PIN_12, IOPORT_PERIPHERAL_BUS),

    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_01, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_02, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_03, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_04, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_05, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_06, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_07, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_09, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_10, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_11, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_12, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_13, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_14, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_06_PIN_15, IOPORT_PERIPHERAL_BUS),

    PIN_PERIPH_HI(BSP_IO_PORT_09_PIN_05, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_09_PIN_06, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_09_PIN_08, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_09_PIN_09, IOPORT_PERIPHERAL_BUS),

    PIN_PERIPH_HI(BSP_IO_PORT_10_PIN_00, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_10_PIN_08, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_10_PIN_09, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_10_PIN_10, IOPORT_PERIPHERAL_BUS),

    /* ---- Phase 2: panel control lines ----------------------------------
       The generator routes P1011 to GPT1 (PWM backlight). Phase 2 drives it
       as a plain GPIO so no r_gpt instance is needed; full brightness.
       Phase 3 flips it to GPT mode at runtime when the camera starts. */
    PIN_GPIO_OUT(BSP_IO_PORT_10_PIN_11, 1),                             /* backlight */
    PIN_GPIO_OUT(BSP_IO_PORT_11_PIN_04, 1),                             /* panel RESET (idle high) */

    /* ---- Phase 3: CEU camera bus (official camera-project pin configs) -- */
    PIN_PERIPH(BSP_IO_PORT_04_PIN_00, IOPORT_PERIPHERAL_CEU),           /* D0 */
    PIN_PERIPH(BSP_IO_PORT_04_PIN_01, IOPORT_PERIPHERAL_CEU),           /* D1 */
    PIN_PERIPH(BSP_IO_PORT_04_PIN_05, IOPORT_PERIPHERAL_CEU),           /* D2 */
    {                                                                   /* D3 (+pullup) */
        .pin     = BSP_IO_PORT_04_PIN_06,
        .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                    (uint32_t) IOPORT_CFG_PULLUP_ENABLE |
                    (uint32_t) IOPORT_PERIPHERAL_CEU),
    },
    PIN_PERIPH(BSP_IO_PORT_07_PIN_00, IOPORT_PERIPHERAL_CEU),           /* D4 */
    PIN_PERIPH(BSP_IO_PORT_07_PIN_01, IOPORT_PERIPHERAL_CEU),           /* D5 */
    PIN_PERIPH(BSP_IO_PORT_07_PIN_02, IOPORT_PERIPHERAL_CEU),           /* D6 */
    PIN_PERIPH(BSP_IO_PORT_07_PIN_03, IOPORT_PERIPHERAL_CEU),           /* D7 */
    {                                                                   /* PCLK (+pullup) */
        .pin     = BSP_IO_PORT_07_PIN_08,
        .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_HIGH |
                    (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                    (uint32_t) IOPORT_CFG_PULLUP_ENABLE |
                    (uint32_t) IOPORT_PERIPHERAL_CEU),
    },
    {                                                                   /* VSYNC (+pullup) */
        .pin     = BSP_IO_PORT_07_PIN_09,
        .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_HIGH |
                    (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                    (uint32_t) IOPORT_CFG_PULLUP_ENABLE |
                    (uint32_t) IOPORT_PERIPHERAL_CEU),
    },
    {                                                                   /* HSYNC (+pullup) */
        .pin     = BSP_IO_PORT_07_PIN_10,
        .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_HIGH |
                    (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                    (uint32_t) IOPORT_CFG_PULLUP_ENABLE |
                    (uint32_t) IOPORT_PERIPHERAL_CEU),
    },
};

static const ioport_cfg_t g_ioport_cfg =
{
    .number_of_pins = (uint16_t) (sizeof(g_bsp_pin_cfg_data) / sizeof(g_bsp_pin_cfg_data[0])),
    .p_pin_cfg_data = g_bsp_pin_cfg_data,
    .p_extend       = NULL,
};

fsp_err_t bsp_pin_init (void)
{
    return R_IOPORT_Open(&g_ioport_ctrl, &g_ioport_cfg);
}

fsp_err_t bsp_pin_cfg (uint16_t pin, uint32_t pin_cfg)
{
    return R_IOPORT_PinCfg(&g_ioport_ctrl, pin, pin_cfg);
}

fsp_err_t bsp_pin_write (uint16_t pin, bsp_io_level_t level)
{
    return R_IOPORT_PinWrite(&g_ioport_ctrl, pin, level);
}

fsp_err_t bsp_pin_read (uint16_t pin, bsp_io_level_t * p_level)
{
    return R_IOPORT_PinRead(&g_ioport_ctrl, pin, p_level);
}
