/**
 * @file bsp_cam.c
 * @brief Camera subsystem: GPT7 XCLK + SCCB scan + OV5640 + CEU capture
 *
 * All instance configuration values are byte-for-byte the official
 * vision_board_camera hal_data.c values (g_timer7 / g_ceu_qvga), with the
 * generated accessor macros replaced by our vector_data.h symbol
 * VECTOR_NUMBER_CEU_CEUI (ICU slot 1 -> EVENT_CEU_CEUI = 0x1DA).
 */
#include "bsp_cam.h"

#include <rtthread.h>

#include "bsp_pin.h"
#include "bsp_sccb.h"
#include "r_ceu.h"
#include "r_gpt.h"
#include "r_ioport.h"

/* ---- XCLK: GPT7 PWM on P1006 (official g_timer7) ------------------------
   PCLKA = 120 MHz, period_counts = 5 -> 24 MHz, duty 2/5 = 40%.
   PIN PROOF (SWD PSEL sweep, 2024 phase-3 debug): with GPT7 running, PWM
   appears on P1006 at PSEL=3 (GTIOC7B) and on NO other PSEL; P1011 is
   GTIOC6B (the official g_timer6 546 us PERIODIC output - it PWMs in the
   official firmware because that BSP also runs channel 6). Muxing P1011
   was the original phase-3 root cause: the XCLK never left the chip and
   every SCCB address NACKed. */
#define CAM_XCLK_PIN        BSP_IO_PORT_10_PIN_06   /* P1006 = GTIOC7B */

static gpt_instance_ctrl_t g_cam_xclk_ctrl;

static const gpt_extended_cfg_t g_cam_xclk_extend =
{
    .gtioca = {.output_enabled = false, .stop_level = GPT_PIN_LEVEL_LOW},
    .gtiocb = {.output_enabled = true,  .stop_level = GPT_PIN_LEVEL_LOW},
    .start_source       = (gpt_source_t) GPT_SOURCE_NONE,
    .stop_source        = (gpt_source_t) GPT_SOURCE_NONE,
    .clear_source       = (gpt_source_t) GPT_SOURCE_NONE,
    .count_up_source    = (gpt_source_t) GPT_SOURCE_NONE,
    .count_down_source  = (gpt_source_t) GPT_SOURCE_NONE,
    .capture_a_source   = (gpt_source_t) GPT_SOURCE_NONE,
    .capture_b_source   = (gpt_source_t) GPT_SOURCE_NONE,
    .capture_a_ipl      = (BSP_IRQ_DISABLED),
    .capture_b_ipl      = (BSP_IRQ_DISABLED),
    .capture_a_irq      = FSP_INVALID_VECTOR,
    .capture_b_irq      = FSP_INVALID_VECTOR,
    .capture_filter_gtioca = GPT_CAPTURE_FILTER_NONE,
    .capture_filter_gtiocb = GPT_CAPTURE_FILTER_NONE,
    .p_pwm_cfg          = NULL,
    .gtior_setting.gtior = 0U,
};

static const timer_cfg_t g_cam_xclk_cfg =
{
    .mode                = TIMER_MODE_PWM,
    .period_counts       = 5U,               /* 120 MHz / 5 = 24 MHz XCLK */
    .duty_cycle_counts   = 2U,               /* 40%                        */
    .source_div          = (timer_source_div_t) 0,
    .channel             = 7,
    .p_callback          = NULL,
    .p_context           = NULL,
    .p_extend            = &g_cam_xclk_extend,
    .cycle_end_ipl       = (BSP_IRQ_DISABLED),
    .cycle_end_irq       = FSP_INVALID_VECTOR,
};

/* ---- CEU instance (official g_ceu_qvga) --------------------------------- */

static ceu_instance_ctrl_t g_cam_ceu_ctrl;

static const ceu_extended_cfg_t g_cam_ceu_extend =
{
    .capture_format     = CEU_CAPTURE_FORMAT_DATA_SYNCHRONOUS,
    .data_bus_width     = CEU_DATA_BUS_SIZE_8_BIT,
    .edge_info          = {.dsel = 0, .hdsel = 0, .vdsel = 0},
    .hsync_polarity     = CEU_HSYNC_POLARITY_HIGH,
    .vsync_polarity     = CEU_VSYNC_POLARITY_HIGH,
    /* Official generator computes all three bits as 1 - replicated. */
    .byte_swapping      = {.swap_8bit_units = 1, .swap_16bit_units = 1,
                           .swap_32bit_units = 1},
    .burst_mode         = CEU_BURST_TRANSFER_MODE_X8,
    .image_area_size    = BSP_CAM_FRAME_BYTES,
    .interrupts_enabled = R_CEU_CEIER_CPEIE_Msk | R_CEU_CEIER_VDIE_Msk |
                          R_CEU_CEIER_CDTOFIE_Msk | R_CEU_CEIER_VBPIE_Msk |
                          R_CEU_CEIER_NHDIE_Msk | R_CEU_CEIER_NVDIE_Msk,
    .ceu_ipl            = (10),
    .ceu_irq            = VECTOR_NUMBER_CEU_CEUI,
};

static void cam_ceu_callback (capture_callback_args_t * p_args);

static const capture_cfg_t g_cam_ceu_cfg =
{
    .x_capture_pixels      = BSP_CAM_WIDTH,
    .y_capture_pixels      = BSP_CAM_HEIGHT,
    .x_capture_start_pixel = 0,
    .y_capture_start_pixel = 0,
    .bytes_per_pixel       = BSP_CAM_BPP,
    .p_callback            = cam_ceu_callback,
    .p_context             = NULL,
    .p_extend              = &g_cam_ceu_extend,
};

static const capture_instance_t g_cam_ceu =
{
    .p_ctrl = &g_cam_ceu_ctrl,
    .p_cfg  = &g_cam_ceu_cfg,
    .p_api  = &g_ceu_on_capture,
};

/* Frame buffer: right after the LCD framebuffer in SDRAM (.nocache_sdram,
   NOLOAD section, D-cache is disabled in this firmware so "nocache" is
   future-proofing, not a current coherency requirement). */
static uint8_t g_cam_frame[BSP_CAM_FRAME_BYTES]
    __attribute__((aligned(32), section(".nocache_sdram")));

/* ---- state --------------------------------------------------------------- */

static bool               g_cam_ready  = false;
static uint8_t            g_cam_addr7  = 0U;
static struct rt_semaphore g_cam_sem;

static void cam_ceu_callback (capture_callback_args_t * p_args)
{
    rt_interrupt_enter();

    if (CEU_EVENT_FRAME_END == p_args->event)
    {
        (void) rt_sem_release(&g_cam_sem);
    }

    rt_interrupt_leave();
}

fsp_err_t bsp_cam_scan (bsp_cam_scan_t * p_out)
{
    fsp_err_t err      = FSP_ERR_NOT_FOUND;
    bool      found    = false;
    uint8_t   addr     = 0U;
    uint16_t  id       = 0U;
    const char * name  = "unknown";

    for (uint8_t a = 0x15U; a <= 0x77U; a++)
    {
        if (!bsp_sccb_probe(a))
        {
            continue;
        }

        found = true;
        addr  = a;

        /* OV5640-class: 16-bit regs, PID at 0x300A/0x300B. */
        if ((FSP_SUCCESS == bsp_ov5640_read_id(a, &id)) &&
            (BSP_OV5640_CHIP_ID == id))
        {
            name = "OV5640";
        }
        else
        {
            uint8_t lo = 0U;

            id = 0U;

            /* OV7725/OV2640-class: 8-bit regs, PID at 0x0A/0x0B. */
            if ((FSP_SUCCESS == bsp_sccb_read8(a, 0x0AU, &lo)))
            {
                id = lo;
                err = bsp_sccb_read8(a, 0x0BU, &lo);
                if (FSP_SUCCESS == err)
                {
                    id = (uint16_t) ((id << 8) | lo);
                    if ((0x7721U == id) || (0x7722U == id))
                    {
                        name = "OV7725";
                    }
                    else if ((0x2641U == id) || (0x2642U == id))
                    {
                        name = "OV2640";
                    }
                }
            }
        }

        break;                       /* report the first ACKing device only */
    }

    if (NULL != p_out)
    {
        p_out->present = found;
        p_out->addr7   = addr;
        p_out->id      = id;
        p_out->name    = name;
    }

    return found ? FSP_SUCCESS : FSP_ERR_NOT_FOUND;
}

fsp_err_t bsp_cam_init (void)
{
    fsp_err_t      err    = FSP_SUCCESS;
    bsp_cam_scan_t scan   = {0};
    static bool    xclk_on = false;

    if (g_cam_ready)
    {
        return FSP_SUCCESS;
    }

    rt_sem_init(&g_cam_sem, "ceusnap", 0, RT_IPC_FLAG_FIFO);

    /* 1. XCLK: mux P1006 (GTIOC7B) to GPT and run 24 MHz.
       P1011 stays a plain GPIO (backlight, full brightness) - it is
       GTIOC6B, not the camera clock. */
    if (!xclk_on)
    {
        err = bsp_pin_cfg(CAM_XCLK_PIN,
                          (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                          (uint32_t) IOPORT_PERIPHERAL_GPT1);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        err = R_GPT_Open(&g_cam_xclk_ctrl, &g_cam_xclk_cfg);
        if ((FSP_SUCCESS != err) && (FSP_ERR_ALREADY_OPEN != err))
        {
            return err;
        }

        R_GPT_Enable(&g_cam_xclk_ctrl);
        err = R_GPT_Start(&g_cam_xclk_ctrl);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        xclk_on = true;
        R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
    }

    /* 2. Power/scan. First a clean power-up + reset pulse per the official
       sensor_probe_init() pre-sequence (PWDN power cycle, then NRST held
       low and released while XCLK is already running) - physical OV5640:
       PWDN active high, NRST active low. The static-level combo cycling
       below is only the fallback for differently-wired modules. */
    bsp_sccb_init();

    bsp_ov5640_power_levels(true,  false);   /* PWDN asserted, NRST held low  */
    bsp_ov5640_power_levels(false, false);   /* PWDN released (up), NRST low  */
    bsp_ov5640_power_levels(false, true);    /* NRST released -> sensor boots */
    R_BSP_SoftwareDelay(20, BSP_DELAY_UNITS_MILLISECONDS);

    static const struct
    {
        bool pwdn_high;
        bool reset_high;
    } combos[4] =
    {
        {false, true},     /* OV5640-style: PWDN low = on, RESET high = run */
        {true,  true},     /* inverted PWDN modules                        */
        {false, false},    /* inverted RESET modules                       */
        {true,  false},    /* both inverted                                */
    };

    bool found = false;

    for (uint32_t i = 0U; i < 4U; i++)
    {
        bsp_ov5640_power_levels(combos[i].pwdn_high, combos[i].reset_high);

        if ((FSP_SUCCESS == bsp_cam_scan(&scan)) && scan.present)
        {
            found = true;
            break;
        }
    }

    rt_kprintf("[cam] scan: %s addr7=0x%02x id=0x%04x (%s)\n",
               found ? "ACK" : "none", scan.addr7, scan.id, scan.name);
    if (!found || (BSP_OV5640_CHIP_ID != scan.id))
    {
        /* addr7 may be 0x36 (SID low) or 0x3C (SID high) - both valid. */
        return FSP_ERR_NOT_FOUND;
    }

    g_cam_addr7 = scan.addr7;

    /* 4. OV5640: default_regs + QVGA RGB565 (register path frozen). */
    err = bsp_ov5640_init_qvga_rgb565(g_cam_addr7);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* 5. CEU open (IRQ slot 1 = EVENT_CEU_CEUI, wired in vector_data.c). */
    err = g_cam_ceu.p_api->open(g_cam_ceu.p_ctrl, g_cam_ceu.p_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_cam_ready = true;
    return FSP_SUCCESS;
}

bool bsp_cam_ready (void)
{
    return g_cam_ready;
}

uint8_t * bsp_cam_framebuffer (void)
{
    return g_cam_frame;
}

fsp_err_t bsp_cam_snap (void)
{
    if (!g_cam_ready)
    {
        return FSP_ERR_NOT_INITIALIZED;
    }

    /* Drop any stale token, then wait for the FRAME_END callback. */
    (void) rt_sem_trytake(&g_cam_sem);

    fsp_err_t err = g_cam_ceu.p_api->captureStart(g_cam_ceu.p_ctrl, g_cam_frame);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (RT_EOK != rt_sem_take(&g_cam_sem, rt_tick_from_millisecond(200)))
    {
        return FSP_ERR_TIMEOUT;
    }

    return FSP_SUCCESS;
}

fsp_err_t bsp_cam_colorbar (bool enable)
{
    if (!g_cam_ready)
    {
        return FSP_ERR_NOT_INITIALIZED;
    }

    return bsp_ov5640_set_colorbar(g_cam_addr7, enable);
}
