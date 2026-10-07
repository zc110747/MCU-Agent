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
 * drain. The generator only used it on P408/P409 for the RGB 4.3" touch bus.
 * Copying it onto any other pin silently kills the output (that is how P208
 * TXD died in Phase 1). No pin in this table needs it any more - the MIPI
 * 2.0" board's touch bus (which must NOT have it) was removed in Phase 7.
 */
#include "bsp_pin.h"

#include "r_ioport.h"

/* ---- helpers: peripheral pin, generator style --------------------------
   NOTE: the parameter is _pin, not pin - a parameter named "pin" would also
   replace the ".pin" designator and produce ".BSP_IO_PORT_02_PIN_08".

   NOTE on the shared PSEL group: P208/P209 are muxed to
   IOPORT_PERIPHERAL_SCI1_3_5_7_9, which is a *group* selector covering
   SCI1/3/5/7/9 - not a per-channel one.  SCI9 (debug console UART) opens the
   pins as a UART; the same group also once carried the SCI3 touch bus. */
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
    /* ---- Phase 1: debug console (SCI9) + LED ----------------------------
       P208/P209 are the SCI9 console UART pins (TXD9/RXD9). */
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

    PIN_PERIPH_HI(BSP_IO_PORT_10_PIN_08, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_10_PIN_09, IOPORT_PERIPHERAL_BUS),
    PIN_PERIPH_HI(BSP_IO_PORT_10_PIN_10, IOPORT_PERIPHERAL_BUS),

    /* ---- Phase 2: panel control lines ----------------------------------
       The generator routes P1011 to GPT1 (PWM backlight). Phase 2 drives it
       as a plain GPIO so no r_gpt instance is needed; full brightness. */
    PIN_GPIO_OUT(BSP_IO_PORT_10_PIN_11, 1),                             /* backlight */
    PIN_GPIO_OUT(BSP_IO_PORT_11_PIN_04, 1),                             /* panel RESET (idle high) */

    /* ---- Phase 7: single-page UI on the MIPI DSI panel -------------------
       The CST812T touch panel was removed from this project: the 2.0" unit
       fitted to this board does not respond to any I2C transaction (see
       documents/phase5-touch-report.md), so P000/P010/P408/P409 are no longer
       configured and the SCI3 I2C block is gone from the build. The panel is
       display-only now; the UI is driven entirely from the debug console. */

    /* ---- Phase 3: OSPI serial flash (single-line SPI) --------------------
       Pin map confirmed against the board schematic:

           OSPI_CS1  = P104    (DRIVE_HIGH)   OSPI_SCLK = P808
           OSPI_SIO0 = P100    (MOSI)         OSPI_SIO1 = P803 (MISO)
           OSPI_SIO2 = P103    (WP)           OSPI_SIO3 = P101 (HOLD/RESET)

       Read those net names as FSP port-pin designators, not as two digits:
       P104 is BSP_IO_PORT_10_PIN_04 (port 10, pin 4) and P100 is
       BSP_IO_PORT_01_PIN_00 (port 1, pin 0).  The two notations are mixed on
       this board and getting one wrong is silent - the write lands on a
       different, unrelated pin and the flash stays unresponsive with a JEDEC
       ID of 00 00 00.

       NOTE: an earlier revision of this file muxed CS0 on P107, taken from
       the reference project's generator output.  The part is fitted to CS1,
       so the driver must be opened with OSPI_B_DEVICE_NUMBER_1.

       NOTE: only six pins are routed and only 1S-1S-1S is usable. There is no
       DS (data strobe) pin, so the driver's auto-calibration path is compiled
       out and SIO2/SIO3 stay as WP/HOLD rather than becoming quad data lines.

       CS carries DRIVE_HIGH in the generated data; keep that flag, the
       reference project sets it deliberately. */
    PIN_PERIPH_HI(BSP_IO_PORT_10_PIN_04, IOPORT_PERIPHERAL_OSPI),  /* CS1 */
    PIN_PERIPH(BSP_IO_PORT_08_PIN_08, IOPORT_PERIPHERAL_OSPI),     /* SCLK */
    PIN_PERIPH(BSP_IO_PORT_01_PIN_00, IOPORT_PERIPHERAL_OSPI),     /* SIO0 / MOSI */
    PIN_PERIPH(BSP_IO_PORT_08_PIN_03, IOPORT_PERIPHERAL_OSPI),     /* SIO1 / MISO */
    PIN_PERIPH(BSP_IO_PORT_01_PIN_03, IOPORT_PERIPHERAL_OSPI),     /* SIO2 / WP */
    PIN_PERIPH(BSP_IO_PORT_01_PIN_01, IOPORT_PERIPHERAL_OSPI),     /* SIO3 / HOLD */
};

/* Re-apply just the mux aspect of the six OSPI pins after a bit-bang probe.
 *
 * The bit-bang fallback in bsp_ospi.c re-slaves these pins to plain GPIO and
 * R_IOPORT_PinCfg() cannot undo that: it writes PmnPFS without touching PSEL
 * (see its implementation - it only guards against clearing PMR while PSEL is
 * set).  PSEL therefore stays 0 after any GPIO reconfigure and no amount of
 * re-initialising the xSPI controller will bring the pads back.  This table
 * rewrites PSEL = 0x1C from scratch with the peripheral-function bit set, which
 * is the only path that restores it.  It is the same data as the OSPI block
 * above, kept separate so a failure of one cannot silently mask the other. */
static const ioport_pin_cfg_t g_bsp_pin_ospi_remux[] =
{
    { .pin = BSP_IO_PORT_10_PIN_04,
      .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_HIGH |
                  (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                  (uint32_t) IOPORT_PERIPHERAL_OSPI) },
    { .pin = BSP_IO_PORT_08_PIN_08,
      .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                  (uint32_t) IOPORT_PERIPHERAL_OSPI) },
    { .pin = BSP_IO_PORT_01_PIN_00,
      .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                  (uint32_t) IOPORT_PERIPHERAL_OSPI) },
    { .pin = BSP_IO_PORT_08_PIN_03,
      .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                  (uint32_t) IOPORT_PERIPHERAL_OSPI) },
    { .pin = BSP_IO_PORT_01_PIN_03,
      .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                  (uint32_t) IOPORT_PERIPHERAL_OSPI) },
    { .pin = BSP_IO_PORT_01_PIN_01,
      .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                  (uint32_t) IOPORT_PERIPHERAL_OSPI) },
};

static const ioport_cfg_t g_bsp_pin_ospi_remux_cfg =
{
    .number_of_pins = (uint16_t) (sizeof(g_bsp_pin_ospi_remux) /
                                  sizeof(g_bsp_pin_ospi_remux[0])),
    .p_pin_cfg_data = g_bsp_pin_ospi_remux,
    .p_extend       = NULL,
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

fsp_err_t bsp_pin_ospi_remux (void)
{
    return R_IOPORT_PinsCfg(&g_ioport_ctrl, &g_bsp_pin_ospi_remux_cfg);
}

fsp_err_t bsp_pin_write (uint16_t pin, bsp_io_level_t level)
{
    return R_IOPORT_PinWrite(&g_ioport_ctrl, pin, level);
}

fsp_err_t bsp_pin_read (uint16_t pin, bsp_io_level_t * p_level)
{
    return R_IOPORT_PinRead(&g_ioport_ctrl, pin, p_level);
}
