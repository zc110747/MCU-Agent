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
   replace the ".pin" designator and produce ".BSP_IO_PORT_02_PIN_08".

   NOTE on the shared PSEL group: P208/P209 are muxed to
   IOPORT_PERIPHERAL_SCI1_3_5_7_9, which is a *group* selector covering
   SCI1/3/5/7/9 - not a per-channel one.  SCI9 (debug console UART) and SCI3
   (touch panel I2C) therefore share the same PSEL value and the same two
   package pins; the console is "uart9" and the touch bus is "sci3i" on the
   official board, both on P208/P209.  The pin is listed once, and the two
   peripherals are told apart by the SCI channel they open. */
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

/* N-channel open-drain peripheral pin.  Currently unused: the RGB 4.3" touch
   bus needed it, the MIPI 2.0" (CST812T) bus must NOT have it - see Phase 6.
   Kept because the warning above is worth having a name for. */
#define PIN_PERIPH_NMOS(_pin, _periph)                     \
    {                                                      \
        .pin     = (_pin),                                 \
        .pin_cfg = ((uint32_t) IOPORT_CFG_NMOS_ENABLE |    \
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
    /* ---- Phase 1: debug console (SCI9) + LED ----------------------------
       P208/P209 are the SCI9 console UART pins (TXD9/RXD9).  The touch bus is
       a *separate* SCI3 pair on P408/P409 - see Phase 6 below.  Both pairs use
       the SCI1_3_5_7_9 group selector, which is why the two listings look
       alike. */
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

    /* ---- Phase 5: MIPI PHY dedicated pin --------------------------------
       The 22 LCD_GRAPHICS pins above are unchanged: the GLCDC still produces
       the parallel RGB stream, it is just bridged internally into the DSI host
       instead of reaching the package pins.  P206 is the one extra pin the
       D-PHY needs, and it is the only pin on the board routed to
       IOPORT_PERIPHERAL_MIPI. */
    PIN_PERIPH(BSP_IO_PORT_02_PIN_06, IOPORT_PERIPHERAL_MIPI),

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
       as a plain GPIO so no r_gpt instance is needed; full brightness. */
    PIN_GPIO_OUT(BSP_IO_PORT_10_PIN_11, 1),                             /* backlight */
    PIN_GPIO_OUT(BSP_IO_PORT_11_PIN_04, 1),                             /* panel RESET (idle high) */

    /* ---- Phase 6: CST812T touch panel (SCI3 I2C + reset/interrupt) -----
       The panel fitted to this board is the MIPI DSI 2.0" (480x360) unit
       carrying a Hynitron CST812T - NOT the GT9147 of the RGB 4.3" variant.
       Its bus is SCI3 on P408/P409, NOT on P208/P209 (those are the SCI9
       console).  Both pin pairs happen to sit in the same IOPORT PSEL *group*
       selector (SCI1_3_5_7_9), which is why the generator emits the same
       group value for both; the channel that actually drives them is decided
       by which SCI is opened (SCI9 = console, SCI3 = touch).

       *** DO NOT add IOPORT_CFG_NMOS_ENABLE here. ***
       The RGB 4.3" variant routes its touch bus through a level shifter and
       needs N-channel open-drain drivers on P408/P409, which is why an
       earlier revision of this table used NMOS.  On the MIPI 2.0" board that
       same bit is fatal: the SCI_B I2C block cannot pull SCL high through an
       NMOS-only driver, so the START condition never completes and
       ISR.IICSTIF stays 0 forever - the transfer times out with no ACK, which
       looks exactly like a missing panel.  Confirmed by comparison against
       the official MIPI 2.0" project (which uses plain
       IOPORT_CFG_PERIPHERAL_PIN for both pins) and by driving SCI3 manually
       over SWD: with NMOS the START never completes, without it IICSTIF=1 and
       IICACKR=1 on the first try.

       RST=P000 is reconfigured to a driven output by bsp_touch_init();
       INT=P010 is left as a plain input (this driver polls). */
    PIN_PERIPH(BSP_IO_PORT_04_PIN_08, IOPORT_PERIPHERAL_SCI1_3_5_7_9),   /* SCL3 */
    PIN_PERIPH(BSP_IO_PORT_04_PIN_09, IOPORT_PERIPHERAL_SCI1_3_5_7_9),   /* SDA3 */
    PIN_GPIO_OUT(BSP_IO_PORT_00_PIN_00, 1),                             /* touch RST (idle high) */
    {                                                                   /* touch INT (input) */
        .pin     = BSP_IO_PORT_00_PIN_10,
        .pin_cfg = ((uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT),
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
