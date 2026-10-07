/**
 * @file mipi_dsi_conf.c
 * @brief MIPI DSI + D-PHY instance config and the 2.0" panel DCS command table
 *
 * This file is the local equivalent of the FSP generator output
 * (`ra_gen/common_data.c`) plus the board port (`board/ports/mipi_lcd/
 * mipi_config.c`) from the official BSP application for this panel:
 *
 *   sdk-bsp-ra8d1-vision-board-master/projects/lvgl/vision_board_mipi_2.0inch_lvgl
 *
 * The instance initialisers and the ~40 entry FocusLCD command table are
 * reproduced verbatim; the console logging was replaced with the project's
 * RT-Thread kprintf so no extra logging dependency is pulled in.
 *
 * Data path:  GLCDC (RGB565 pipeline)  ->  DSI (2 lanes, video mode)  ->  PHY
 */
#include "mipi_dsi_conf.h"

#include <rtthread.h>

#include "r_display_api.h"   /* DISPLAY_SIGNAL_POLARITY_* for the sync polarity */
#include "vector_data.h"

/* Defined further down; the config struct needs the address before it. */
void mipi_dsi0_callback (mipi_dsi_callback_args_t * p_args);

/* =========================================================== D-PHY instance */

/* D-PHY power-mode transition timings (R01UH0995EJ0060 fig. 57.1).
   Values are verbatim from the official BSP. */
#define MIPI_PHY_CLKSTPT   (1183)
#define MIPI_PHY_CLKBFHT   (10 + 1)
#define MIPI_PHY_CLKKPT    (22 + 4)
#define MIPI_PHY_GOLPBKT   (40)

#define MIPI_PHY_TINIT     (71999)
#define MIPI_PHY_TCLKPREP  (8)
#define MIPI_PHY_THSPREP   (5)
#define MIPI_PHY_TCLKTRAIL (7)
#define MIPI_PHY_TCLKPOST  (19)
#define MIPI_PHY_TCLKPRE   (1)
#define MIPI_PHY_TCLKZERO  (27)
#define MIPI_PHY_THSEXIT   (11)
#define MIPI_PHY_THSTRAIL  (8)
#define MIPI_PHY_THSZERO   (19)
#define MIPI_PHY_TLPEXIT   (7)

static const mipi_phy_timing_t g_mipi_phy0_timing =
{
    .t_init                 = 0x3FFFF & (uint32_t) MIPI_PHY_TINIT,
    .t_clk_prep             = (uint8_t) MIPI_PHY_TCLKPREP,
    .t_hs_prep              = (uint8_t) MIPI_PHY_THSPREP,
    .dphytim4_b.t_clk_trail = (uint32_t) MIPI_PHY_TCLKTRAIL,
    .dphytim4_b.t_clk_post  = (uint32_t) MIPI_PHY_TCLKPOST,
    .dphytim4_b.t_clk_pre   = (uint32_t) MIPI_PHY_TCLKPRE,
    .dphytim4_b.t_clk_zero  = (uint32_t) MIPI_PHY_TCLKZERO,
    .dphytim5_b.t_hs_exit   = (uint32_t) MIPI_PHY_THSEXIT,
    .dphytim5_b.t_hs_trail  = (uint32_t) MIPI_PHY_THSTRAIL,
    .dphytim5_b.t_hs_zero   = (uint32_t) MIPI_PHY_THSZERO,
    .t_lp_exit              = (uint32_t) MIPI_PHY_TLPEXIT,
};

static mipi_phy_ctrl_t g_mipi_phy0_ctrl;

/* PLL: 20 MHz reference / (1) * 50 = 1000 MHz, 0.00% error.  `div` and
   `mul_int` are programmed 1-based, hence the "- 1". */
static const mipi_phy_cfg_t g_mipi_phy0_cfg =
{
    .pll_settings = { .div = 1 - 1, .mul_int = 50 - 1, .mul_frac = 0 },
    .lp_divisor   = 5 - 1,
    .p_timing     = &g_mipi_phy0_timing,
};

const mipi_phy_instance_t g_mipi_phy0 =
{
    .p_ctrl = &g_mipi_phy0_ctrl,
    .p_cfg  = &g_mipi_phy0_cfg,
    .p_api  = &g_mipi_phy,
};

/* =============================================================== DSI host */

mipi_dsi_instance_ctrl_t g_mipi_dsi0_ctrl;

static const mipi_dsi_timing_t g_mipi_dsi0_timing =
{
    .clock_stop_time       = MIPI_PHY_CLKSTPT,
    .clock_beforehand_time = MIPI_PHY_CLKBFHT,
    .clock_keep_time       = MIPI_PHY_CLKKPT,
    .go_lp_and_back        = MIPI_PHY_GOLPBKT,
};

/* Six DSI interrupt slots.  Their vector numbers come from vector_data.h and
   must match the `g_vector_table[]` / `g_interrupt_event_link_select[]`
   entries in bsp/ra8d1-vision-board/gen/vector_data.c. */
static const mipi_dsi_extended_cfg_t g_mipi_dsi0_extended_cfg =
{
    .dsi_seq0.ipl = (12),
    .dsi_seq0.irq = VECTOR_NUMBER_MIPI_DSI_SEQ0,

    .dsi_seq1.ipl = (12),
    .dsi_seq1.irq = VECTOR_NUMBER_MIPI_DSI_SEQ1,

    .dsi_vin1.ipl = (12),
    .dsi_vin1.irq = VECTOR_NUMBER_MIPI_DSI_VIN1,

    .dsi_rcv.ipl = (12),
    .dsi_rcv.irq = VECTOR_NUMBER_MIPI_DSI_RCV,

    .dsi_ferr.ipl = (12),
    .dsi_ferr.irq = VECTOR_NUMBER_MIPI_DSI_FERR,

    .dsi_ppi.ipl = (12),
    .dsi_ppi.irq = VECTOR_NUMBER_MIPI_DSI_PPI,

    .dsi_rxie    = R_DSILINK_RXIER_BTAREND_Msk | R_DSILINK_RXIER_LRXHTO_Msk | R_DSILINK_RXIER_TATO_Msk | R_DSILINK_RXIER_RXRESP_Msk | R_DSILINK_RXIER_RXEOTP_Msk | R_DSILINK_RXIER_RXTE_Msk | R_DSILINK_RXIER_RXACK_Msk | R_DSILINK_RXIER_EXTEDET_Msk | R_DSILINK_RXIER_MLFERR_Msk | R_DSILINK_RXIER_ECCERRM_Msk | R_DSILINK_RXIER_UNEXERR_Msk | R_DSILINK_RXIER_WCERR_Msk | R_DSILINK_RXIER_CRCERR_Msk | R_DSILINK_RXIER_IBERR_Msk | R_DSILINK_RXIER_RXOVFERR_Msk | R_DSILINK_RXIER_PRTOERR_Msk | R_DSILINK_RXIER_NORESERR_Msk | R_DSILINK_RXIER_RSIZEERR_Msk | R_DSILINK_RXIER_ECCERRS_Msk | R_DSILINK_RXIER_RXAKE_Msk | 0x0,
    .dsi_ferrie  = R_DSILINK_FERRIER_HTXTO_Msk | R_DSILINK_FERRIER_LRXHTO_Msk | R_DSILINK_FERRIER_TATO_Msk | R_DSILINK_FERRIER_ESCENT_Msk | R_DSILINK_FERRIER_SYNCESC_Msk | R_DSILINK_FERRIER_CTRL_Msk | R_DSILINK_FERRIER_CLP0_Msk | R_DSILINK_FERRIER_CLP1_Msk | 0x0,
    .dsi_plie    = 0x0,
    .dsi_vmie    = R_DSILINK_VMIER_VBUFUDF_Msk | R_DSILINK_VMIER_VBUFOVF_Msk | 0x0,
    .dsi_sqch0ie = R_DSILINK_SQCH0IER_AACTFIN_Msk | R_DSILINK_SQCH0IER_ADESFIN_Msk | R_DSILINK_SQCH0IER_TXIBERR_Msk | R_DSILINK_SQCH0IER_RXFERR_Msk | R_DSILINK_SQCH0IER_RXFAIL_Msk | R_DSILINK_SQCH0IER_RXPFAIL_Msk | R_DSILINK_SQCH0IER_RXCORERR_Msk | R_DSILINK_SQCH0IER_RXAKE_Msk | 0x0,
    .dsi_sqch1ie = R_DSILINK_SQCH1IER_AACTFIN_Msk | R_DSILINK_SQCH1IER_ADESFIN_Msk | R_DSILINK_SQCH1IER_SIZEERR_Msk | R_DSILINK_SQCH1IER_TXIBERR_Msk | R_DSILINK_SQCH1IER_RXFERR_Msk | R_DSILINK_SQCH1IER_RXFAIL_Msk | R_DSILINK_SQCH1IER_RXPFAIL_Msk | R_DSILINK_SQCH1IER_RXCORERR_Msk | R_DSILINK_SQCH1IER_RXAKE_Msk | 0x0,
};

/* Video timing, 480x360.  The porches are written as the "total minus
   active minus sync" expressions FSP emits so the intent stays readable:
     vertical:   total 382, active 360, sync 4, back porch 10  -> fp 8
     horizontal: total 514, active 480, sync 4, back porch 20  -> fp 10 */
static const mipi_dsi_cfg_t g_mipi_dsi0_cfg =
{
    .p_mipi_phy_instance = &g_mipi_phy0,
    .p_timing            = &g_mipi_dsi0_timing,
    .p_callback          = mipi_dsi0_callback,
    .p_context           = NULL,
    .p_extend            = &g_mipi_dsi0_extended_cfg,

    .sync_pulse         = (0),
    .data_type          = MIPI_DSI_VIDEO_DATA_16RGB_PIXEL_STREAM,
    .virtual_channel_id = 0,

    .vertical_active_lines  = 360,
    .vertical_sync_lines    = 4,
    .vertical_back_porch    = (10 - 4),
    .vertical_front_porch   = (382 - 360 - 10 - 4),
    .vertical_sync_polarity = (DISPLAY_SIGNAL_POLARITY_LOACTIVE != DISPLAY_SIGNAL_POLARITY_HIACTIVE),

    .horizontal_active_lines  = 480,
    .horizontal_sync_lines    = 4,
    .horizontal_back_porch    = (20 - 4),
    .horizontal_front_porch   = (514 - 480 - 20 - 4),
    .horizontal_sync_polarity = (DISPLAY_SIGNAL_POLARITY_LOACTIVE != DISPLAY_SIGNAL_POLARITY_HIACTIVE),

    .video_mode_delay = 206,

    .hsa_no_lp = ((0x0) & R_DSILINK_VMSET0R_HSANOLP_Msk),
    .hbp_no_lp = ((0x0) & R_DSILINK_VMSET0R_HBPNOLP_Msk),
    .hfp_no_lp = ((0x0) & R_DSILINK_VMSET0R_HFPNOLP_Msk),

    .num_lanes          = 2,
    .ulps_wakeup_period = 97,
    .continuous_clock   = (1),

    .hs_tx_timeout      = 0,
    .lp_rx_timeout      = 0,
    .turnaround_timeout = 0,
    .bta_timeout        = 0,
    .lprw_timeout       = (0 << R_DSILINK_PRESPTOLPSETR_LPRTO_Pos) | 0,
    .hsrw_timeout       = (0 << R_DSILINK_PRESPTOHSSETR_HSRTO_Pos) | 0,

    .max_return_packet_size = 1,
    .ecc_enable             = (1),
    .crc_check_mask         = (mipi_dsi_vc_t) (0x0),
    .scramble_enable        = (0),
    .tearing_detect         = (0),
    .eotp_enable            = (1),
};

const mipi_dsi_instance_t g_mipi_dsi0 =
{
    .p_ctrl = &g_mipi_dsi0_ctrl,
    .p_cfg  = &g_mipi_dsi0_cfg,
    .p_api  = &g_mipi_dsi,
};

/* ===================================================== panel DCS command table */

/* Sentinel command ids used by the table format. */
#define MIPI_DSI_TABLE_DATA_DELAY_FLAG   ((mipi_dsi_cmd_id_t) 0xFE)
#define MIPI_DSI_TABLE_END_OF_TABLE      ((mipi_dsi_cmd_id_t) 0xFD)

typedef struct
{
    uint8_t           size;
    uint8_t           buffer[20];
    mipi_dsi_cmd_id_t cmd_id;
    mipi_dsi_cmd_flag_t flags;
} lcd_table_setting_t;

/* The DSI sequence callback fires as each command descriptor chain finishes.
   The table pusher blocks on this flag, so it must be volatile. */
static volatile bool     g_message_sent = false;
static volatile uint32_t g_phy_status   = 0U;
static volatile uint32_t g_cmd_count    = 0U;
static volatile uint32_t g_seq0_count   = 0U;

void mipi_dsi0_callback (mipi_dsi_callback_args_t * p_args)
{
    switch (p_args->event)
    {
        case MIPI_DSI_EVENT_SEQUENCE_0:
        {
            /* tx_status is the raw SQCH0SR value.  The BSP generator and the
               official reference both compare for equality, so keep that
               semantic - it is what the panel bring-up was validated with. */
            g_seq0_count++;
            if (MIPI_DSI_SEQUENCE_STATUS_DESCRIPTORS_FINISHED == p_args->tx_status)
            {
                g_message_sent = true;
            }
            break;
        }

        case MIPI_DSI_EVENT_PHY:
        {
            g_phy_status |= p_args->phy_status;
            break;
        }

        default:
        {
            break;
        }
    }
}

/* FocusLCD 2.0" 480x360 initialisation sequence - verbatim from the official
   BSP (board/ports/mipi_lcd/mipi_config.c).  BK3/BK0/BK1 are command banks
   inside the panel controller, selected by writing 0xFF 0x77 0x01 0x00 0x00
   followed by the bank id.  The table ends with Sleep Out (0x11, 120 ms),
   Display On (0x29) and the 0xFD terminator. */
static const lcd_table_setting_t g_lcd_init_focuslcd[] =
{
    /* enable the BK function of Command2: BK3 */
    {6,     {0xFF, 0x77, 0x01, 0x00, 0x00, 0x13},   MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {2,     {0xEF, 0x08},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

    /* enable the BK function of Command2: BK0 */
    {6,     {0xFF, 0x77, 0x01, 0x00, 0x00, 0x10},   MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Display Line Setting: SCNL = (44Line+1)*8 = 360 */
    {3,     {0xC0, 0x2C, 0x00},                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Porch Control: VBP = 13, VFP = 2 */
    {3,     {0xC1, 0x0D, 0x02},                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Inversion selection & frame rate: 2-dot inversion, min pclk per line 5 */
    {3,     {0xC2, 0x31, 0x05},                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* 0xC3 left at its default: RGB DE mode, low-active syncs */
    {2,     {0xCC, 0x10},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Positive / Negative gamma */
    {17,    {0xB0, 0x0A, 0x14, 0x1B, 0x0D, 0x10, 0x05, 0x07, 0x08, 0x06, 0x22, 0x03, 0x11, 0x10, 0xAD, 0x31, 0x1B}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {17,    {0xB1, 0x0A, 0x14, 0x1B, 0x0D, 0x10, 0x05, 0x07, 0x08, 0x06, 0x22, 0x03, 0x11, 0x10, 0xAD, 0x31, 0x1B}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},

    /* enable the BK function of Command2: BK1 */
    {6,     {0xFF, 0x77, 0x01, 0x00, 0x00, 0x11},   MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Vop amplitude: VRH = 80 */
    {2,     {0xB0, 0x50},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* VCOM amplitude: 94 (1.275 V) */
    {2,     {0xB1, 0x5E},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* VGH voltage: 15 V */
    {2,     {0xB2, 0x87},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* TEST command */
    {2,     {0xB3, 0x80},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* VGL voltage: -9.51 V */
    {2,     {0xB5, 0x47},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Power control 1: gamma OP bias currents at minimum */
    {2,     {0xB7, 0x85},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Power control 2: AVDD 6.6 V, AVCL -4.6 V */
    {2,     {0xB8, 0x21},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Source pre-drive timing set1: 8 (1.6 us) */
    {2,     {0xC1, 0x78},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Source EQ2 setting */
    {2,     {0xC2, 0x78},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* MIPI setting 1: EOT_EN = 1, ERR_SEL = 0 */
    {2,     {0xD0, 0x88},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {2,     {0xE0, 0x00},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {2,     {0x1B, 0x02},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {12,    {0xE1, 0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44},       MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {13,    {0xE2, 0x11, 0x11, 0x44, 0x44, 0x75, 0xA0, 0x00, 0x00, 0x74, 0xA0, 0x00, 0x00}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {5,     {0xE3, 0x00, 0x00, 0x11, 0x11},         MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {3,     {0xE4, 0x44, 0x44},                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {17,    {0xE5, 0x0A, 0x71, 0xD8, 0xA0, 0x0C, 0x73, 0xD8, 0xA0, 0x0E, 0x75, 0xD8, 0xA0, 0x10, 0x77, 0xD8, 0xA0}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {5,     {0xE6, 0x00, 0x00, 0x11, 0x11},         MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {3,     {0xE7, 0x44, 0x44},                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {17,    {0xE8, 0x09, 0x70, 0xD8, 0xA0, 0x0B, 0x72, 0xD8, 0xA0, 0x0D, 0x74, 0xD8, 0xA0, 0x0F, 0x76, 0xD8, 0xA0}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {8,     {0xEB, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40},                                                       MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {3,     {0xEC, 0x3C, 0x00},                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {17,    {0xED, 0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {7,     {0xEF, 0x08, 0x08, 0x08, 0x45, 0x3F, 0x54},                                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},

    /* enable the BK function of Command2: BK3 */
    {6,     {0xFF, 0x77, 0x01, 0x00, 0x00, 0x13},   MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {3,     {0xE8, 0x00, 0x0E},                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {3,     {0xE8, 0x00, 0x0C},                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {3,     {0xE8, 0x00, 0x00},                     MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},

    /* disable the BK function of Command2 */
    {6,     {0xFF, 0x77, 0x01, 0x00, 0x00, 0x00},   MIPI_DSI_CMD_ID_DCS_LONG_WRITE, MIPI_DSI_CMD_FLAG_LOW_POWER},

    /* Interface pixel format: 0x55 = 16 bit/pixel */
    {2,     {0x3A, 0x55},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Display data access control: normal scan, RGB order */
    {2,     {0x36, 0x40},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    /* Sleep Out */
    {2,     {0x11, 0x00},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_0_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
    {120,   {0},                                    MIPI_DSI_TABLE_DATA_DELAY_FLAG, (mipi_dsi_cmd_flag_t) 0},
    /* Display On */
    {2,     {0x29, 0x00},                           MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_0_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

    {0x00,  {0},                                    MIPI_DSI_TABLE_END_OF_TABLE, (mipi_dsi_cmd_flag_t) 0},
};

/* Push the whole table, one command at a time.
 *
 * NOTE on the delay entry: the official implementation reads `table->size`
 * (the first entry of the table) instead of `p_entry->size`.  Both happen to
 * be the same value (6) in the current table, so the bug is latent - this
 * version uses the correct `p_entry` so a future edit to the table cannot
 * silently change the delay. */
static void mipi_dsi_push_table (const lcd_table_setting_t * table)
{
    const lcd_table_setting_t * p_entry = table;

    while (MIPI_DSI_TABLE_END_OF_TABLE != p_entry->cmd_id)
    {
        mipi_dsi_cmd_t msg =
        {
            .channel      = 0,
            .cmd_id       = p_entry->cmd_id,
            .flags        = p_entry->flags,
            .tx_len       = p_entry->size,
            .p_tx_buffer  = p_entry->buffer,
        };

        if (MIPI_DSI_TABLE_DATA_DELAY_FLAG == msg.cmd_id)
        {
            R_BSP_SoftwareDelay(p_entry->size, BSP_DELAY_UNITS_MILLISECONDS);
        }
        else
        {
            fsp_err_t err;

            g_message_sent = false;
            err = R_MIPI_DSI_Command(&g_mipi_dsi0_ctrl, &msg);
            if (FSP_SUCCESS != err)
            {
                rt_kprintf("[mipi] DCS 0x%02x failed: 0x%x\n",
                           (unsigned int) p_entry->buffer[0], (unsigned int) err);
            }

            /* Bounded wait: a missing SEQ0 vector would otherwise hang the
               boot for good.  100 ms is far more than a DCS command takes. */
            {
                uint32_t guard = 100U;

                while ((!g_message_sent) && (guard > 0U))
                {
                    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
                    guard--;
                }

                if (0U == guard)
                {
                    rt_kprintf("[mipi] timeout waiting for cmd 0x%02x (seq0=%u)\n",
                               (unsigned int) p_entry->buffer[0],
                               (unsigned int) g_seq0_count);
                }
            }
        }

        g_cmd_count++;
        p_entry++;
    }
}

void ra8_mipi_lcd_init (void)
{
    g_cmd_count = 0U;
    g_seq0_count = 0U;
    mipi_dsi_push_table(g_lcd_init_focuslcd);
    rt_kprintf("[mipi] %u DCS commands pushed, seq0=%u, phy_status=0x%x\n",
               (unsigned int) g_cmd_count, (unsigned int) g_seq0_count,
               (unsigned int) g_phy_status);
}

uint32_t ra8_mipi_cmd_count (void)
{
    return g_cmd_count;
}

uint32_t ra8_mipi_seq0_count (void)
{
    return g_seq0_count;
}

uint32_t ra8_mipi_phy_status (void)
{
    return g_phy_status;
}
