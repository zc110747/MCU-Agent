/**
 * @file bsp_lcd.c
 * @brief GLCDC + MIPI DSI wiring for the onboard 2.0" 480x360 panel
 *
 * Phase 2 drove a parallel RGB666 panel straight from the GLCDC pins.  The
 * panel actually fitted to this board is a MIPI DSI unit, which is why Phase 2
 * produced a lit-but-black screen no matter what the framebuffer contained.
 *
 * Phase 5 therefore keeps the GLCDC as the pixel pipeline but does not let it
 * reach the package pins: `g_display_extend_cfg.phy_layer` points at the DSI
 * host, so the RGB stream is handed over internally and serialised over two
 * D-PHY lanes.  The panel's own DCS power-on table is pushed by
 * ra8_mipi_lcd_init() between R_GLCDC_Open() and R_GLCDC_Start().
 *
 * Everything here (timing, format, polarity, tcon mapping, PHY PLL) is taken
 * verbatim from the official BSP application that drives this exact panel:
 *   projects/lvgl/vision_board_mipi_2.0inch_lvgl/ra_gen/common_data.c
 * The only intentional difference is the framebuffer placement, see below.
 *
 * Framebuffer placement:
 *   .sdram (0x68000000). The GLCDC is a second bus master on the same memory,
 *   so the buffer must not be cached -- BSP_CFG_DCACHE_ENABLED is 0 in this
 *   project, which makes the whole SDRAM window effectively coherent. If the
 *   D-cache is ever enabled, the framebuffer MUST move to .nocache_sdram
 *   (the only SDRAM area system.c maps non-cacheable) or be cleaned by hand.
 */
#include "bsp_lcd.h"

#include "bsp_api.h"
#include "board_sdram.h"
#include "mipi_dsi_conf.h"
#include "r_glcdc.h"
#include "rtthread.h"

/* Panel control lines. See documents/pinmap.md and bsp_pin.c. */
#define BSP_LCD_PIN_BL       BSP_IO_PORT_10_PIN_11
#define BSP_LCD_PIN_RESET    BSP_IO_PORT_11_PIN_04

#define BSP_LCD_LAYER        (0)

/* GLCDC frame-boundary callback; the config struct below needs its address. */
void DisplayVsyncCallback (display_callback_args_t * p_args);

/* -------------------------------------------------------------- framebuffer */
/* Two full-screen pages, 345,600 B each.  .sdram is a NOLOAD section, so this
   costs no flash and no internal RAM.  DRW2D can play with stride=pixels: the
   generator's alignment formula collapses to the identity at 480x16bpp (see
   BSP_LCD_STRIDE), which is checked at compile time below. */
#define BSP_LCD_FB_PIXELS    (BSP_LCD_STRIDE * BSP_LCD_HEIGHT)

static uint16_t g_lcd_fb[BSP_LCD_FB_PAGES][BSP_LCD_FB_PIXELS]
    BSP_ALIGN_VARIABLE(64) BSP_PLACE_IN_SECTION(".sdram");

/* The GLCDC HSTRIDE field is in pixels and the generator rounds it up to a
   512-bit (64 byte) boundary first.  Assert the two agree, so a geometry edit
   that breaks the identity fails the build instead of the screen. */
#define BSP_LCD_STRIDE_BYTES_GEN  ((((BSP_LCD_WIDTH * BSP_LCD_BPP) + 0x1FFU) >> 9U) << 6U)
#define BSP_LCD_STRIDE_PIX_GEN    ((BSP_LCD_STRIDE_BYTES_GEN * 8U) / BSP_LCD_BPP)
_Static_assert(BSP_LCD_STRIDE_PIX_GEN == BSP_LCD_STRIDE,
               "GLCDC hstride must match the generator's 64-byte alignment rule");

/* ------------------------------------------------------------ GLCDC objects */
static glcdc_instance_ctrl_t g_display_ctrl;

/* TCON pin mapping differs from the RGB variant: the DSI bridge feeds the
   pixel stream through TCON_1/0/2 rather than TCON_0/1/3. */
static const glcdc_extended_cfg_t g_display_extend_cfg =
{
    .tcon_hsync            = GLCDC_TCON_PIN_1,
    .tcon_vsync            = GLCDC_TCON_PIN_0,
    .tcon_de               = GLCDC_TCON_PIN_2,
    .correction_proc_order = GLCDC_CORRECTION_PROC_ORDER_BRIGHTNESS_CONTRAST2GAMMA,
    .clksrc                = GLCDC_CLK_SRC_INTERNAL,
    .clock_div_ratio       = GLCDC_PANEL_CLK_DIVISOR_8,
    .dithering_mode        = GLCDC_DITHERING_MODE_TRUNCATE,
    .dithering_pattern_A   = GLCDC_DITHERING_PATTERN_11,
    .dithering_pattern_B   = GLCDC_DITHERING_PATTERN_11,
    .dithering_pattern_C   = GLCDC_DITHERING_PATTERN_11,
    .dithering_pattern_D   = GLCDC_DITHERING_PATTERN_11,
    /* The bridge: hand the RGB stream to the DSI host, not to the pins. */
    .phy_layer             = (void *) &g_mipi_dsi0,
};

static const display_cfg_t g_display_cfg =
{
    /* Graphics layer 1 (background) - the only layer this project uses. */
    .input[0] =
    {
        .p_base                  = (uint32_t *) &g_lcd_fb[0][0],
        .hsize                   = BSP_LCD_WIDTH,
        .vsize                   = BSP_LCD_HEIGHT,
        .hstride                 = BSP_LCD_STRIDE,
        .format                  = DISPLAY_IN_FORMAT_16BITS_RGB565,
        .line_descending_enable  = false,
        .lines_repeat_enable     = false,
        .lines_repeat_times      = 0,
    },

    /* Graphics layer 2 - unused. Written out in full: a bare "{0}" makes GCC
       complain -Wmissing-braces because the first member is an aggregate. */
    .input[1] =
    {
        .p_base = NULL,
    },

    .layer[0] =
    {
        .coordinate  = {0, 0},
        .bg_color    = {.byte = {.a = 255, .r = 255, .g = 255, .b = 255}},
        .fade_control = DISPLAY_FADE_CONTROL_NONE,
        .fade_speed   = 0,
    },
    .layer[1] =
    {
        .coordinate   = {0, 0},
        .bg_color     = {.argb = 0},
        .fade_control = DISPLAY_FADE_CONTROL_NONE,
        .fade_speed   = 0,
    },

    /* Output timing / format straight from the official MIPI application:
       514x382 total, 480x360 visible, 4 cycle syncs, low-active,
       RGB565 little endian, RGB colour order. */
    .output =
    {
        .htiming =
        {
            .total_cyc          = 514,
            .display_cyc        = 480,
            .back_porch         = 20,
            .sync_width         = 4,
            .sync_polarity      = DISPLAY_SIGNAL_POLARITY_LOACTIVE,
        },
        .vtiming =
        {
            .total_cyc          = 382,
            .display_cyc        = 360,
            .back_porch         = 10,
            .sync_width         = 4,
            .sync_polarity      = DISPLAY_SIGNAL_POLARITY_LOACTIVE,
        },
        .format                = DISPLAY_OUT_FORMAT_16BITS_RGB565,
        .endian                = DISPLAY_ENDIAN_LITTLE,
        .color_order           = DISPLAY_COLOR_ORDER_RGB,
        .data_enable_polarity  = DISPLAY_SIGNAL_POLARITY_HIACTIVE,
        .sync_edge             = DISPLAY_SIGNAL_SYNC_EDGE_RISING,
        .bg_color              = {.byte = {.a = 255, .r = 0, .g = 0, .b = 0}},
        .brightness            = {.enable = false, .r = 512, .g = 512, .b = 512},
        .contrast              = {.enable = false, .r = 128, .g = 128, .b = 128},
        .p_gamma_correction    = NULL,
        .dithering_on          = false,
    },

    /* The line-detect interrupt is the frame boundary LVGL synchronises to
       (tear-free page swap).  Running, not disabled as in Phase 2. */
    .line_detect_ipl         = (12),
    .underflow_1_ipl         = BSP_IRQ_DISABLED,
    .underflow_2_ipl         = BSP_IRQ_DISABLED,
    .line_detect_irq         = VECTOR_NUMBER_GLCDC_LINE_DETECT,
    .underflow_1_irq         = FSP_INVALID_VECTOR,
    .underflow_2_irq         = FSP_INVALID_VECTOR,

    .p_callback              = DisplayVsyncCallback,
    .p_context               = NULL,
    .p_extend                = &g_display_extend_cfg,
};

/* ------------------------------------------------------------ runtime state */
static volatile bool     g_lcd_ready = false;
static volatile uint32_t g_vsync_count = 0U;

/* GLCDC frame-boundary callback (declared weak in the header contract so LVGL
   can reuse it; here it just counts frames). */
void DisplayVsyncCallback (display_callback_args_t * p_args)
{
    if (DISPLAY_EVENT_LINE_DETECTION == p_args->event)
    {
        g_vsync_count++;
        bsp_lcd_vsync_notify();
    }
}

/* ------------------------------------------------------------------ helpers */
static uint16_t rgb888_to_565 (uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t) ((((uint16_t) r & 0xF8U) << 8) |
                       (((uint16_t) g & 0xFCU) << 3) |
                       (((uint16_t) b & 0xF8U) >> 3));
}

/* ------------------------------------------------------------------- public */
fsp_err_t bsp_lcd_init (void)
{
    fsp_err_t err;

    /* The framebuffer lives in SDRAM: bring the SDRAM controller up first. */
    bsp_sdram_init();

    /* Panel reset: low pulse, then idle high. */
    R_BSP_PinAccessEnable();
    R_BSP_PinWrite(BSP_LCD_PIN_RESET, false);
    R_BSP_SoftwareDelay(20, BSP_DELAY_UNITS_MILLISECONDS);
    R_BSP_PinWrite(BSP_LCD_PIN_RESET, true);
    R_BSP_SoftwareDelay(20, BSP_DELAY_UNITS_MILLISECONDS);

    /* Both pages start black so the panel never shows stale SDRAM content. */
    bsp_lcd_fill(0x0000U);
    for (uint32_t i = 0U; i < BSP_LCD_FB_PIXELS; i++)
    {
        g_lcd_fb[1][i] = 0x0000U;
    }

    /* R_GLCDC_Open() opens the DSI host and the D-PHY through `phy_layer`
       before touching the GLCDC itself. */
    err = R_GLCDC_Open(&g_display_ctrl, &g_display_cfg);
    if (FSP_SUCCESS != err)
    {
        rt_kprintf("[lcd] R_GLCDC_Open failed: 0x%x\n", (unsigned int) err);
        return err;
    }

    /* Panel DCS power-on table.  Must run after Open (DSI up) and before
       Start (so the panel is already scanning when the first frame lands). */
    ra8_mipi_lcd_init();

    err = bsp_lcd_start();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* Interrupts are wired now, but clear the sticky flags once anyway: from
       here on a set L1UNDF bit means layer 1 really failed to fetch.

       NOTE: STMON.L2UNDF reads 1 permanently and re-asserts ~1-2 frame periods
       after a STCLR clear (measured over SWD, tools/verify/probe_stmon.py).
       This is a benign hardware artifact: the FSP driver arms the GR[1]
       line-detect position unconditionally (r_glcdc.c CLUTINT_b.LINE) while
       layer 2 is transparent with FLMRD.RENB=0 (no memory fetch). Layer 1 --
       the only layer that fetches -- never underflows (L1UNDF=0), so
       acceptance checks L1UNDF only. */
    R_GLCDC->SYSCNT.STCLR_b.VPOSCLR   = 1U;
    R_GLCDC->SYSCNT.STCLR_b.L1UNDFCLR = 1U;
    R_GLCDC->SYSCNT.STCLR_b.L2UNDFCLR = 1U;

    bsp_lcd_set_backlight(true);
    g_lcd_ready = true;

    rt_kprintf("[lcd] GLCDC %ux%u up, panel=%ux%u, fb=0x%08x+0x%08x\n",
               (unsigned int) BSP_LCD_WIDTH, (unsigned int) BSP_LCD_HEIGHT,
               (unsigned int) MIPI_PANEL_WIDTH, (unsigned int) MIPI_PANEL_HEIGHT,
               (unsigned int) (uint32_t) &g_lcd_fb[0][0],
               (unsigned int) (uint32_t) &g_lcd_fb[1][0]);

    return FSP_SUCCESS;
}

fsp_err_t bsp_lcd_start (void)
{
    return R_GLCDC_Start(&g_display_ctrl);
}

uint16_t * bsp_lcd_framebuffer (void)
{
    return &g_lcd_fb[0][0];
}

uint16_t * bsp_lcd_framebuffer_page (uint32_t index)
{
    if (index >= BSP_LCD_FB_PAGES)
    {
        return NULL;
    }

    return &g_lcd_fb[index][0];
}

bool bsp_lcd_ready (void)
{
    return g_lcd_ready;
}

fsp_err_t bsp_lcd_set_framebuffer (void * buffer)
{
    return R_GLCDC_BufferChange(&g_display_ctrl, (uint8_t *) buffer,
                                (display_frame_layer_t) BSP_LCD_LAYER);
}

void bsp_lcd_vsync_notify (void)
{
    extern void lv_port_vsync_notify (void);

    lv_port_vsync_notify();
}

uint32_t bsp_lcd_vsync_count (void)
{
    return g_vsync_count;
}

void bsp_lcd_fill (uint16_t color)
{
    bsp_lcd_fill_rect(0, 0, BSP_LCD_WIDTH, BSP_LCD_HEIGHT, color);
}

void bsp_lcd_fill_rect (uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    if ((x >= BSP_LCD_WIDTH) || (y >= BSP_LCD_HEIGHT))
    {
        return;
    }

    if ((x + w) > BSP_LCD_WIDTH)
    {
        w = BSP_LCD_WIDTH - x;
    }

    if ((y + h) > BSP_LCD_HEIGHT)
    {
        h = BSP_LCD_HEIGHT - y;
    }

    for (uint32_t row = 0; row < h; row++)
    {
        uint16_t * p = &g_lcd_fb[0][((y + row) * BSP_LCD_STRIDE) + x];

        for (uint32_t col = 0; col < w; col++)
        {
            p[col] = color;
        }
    }
}

void bsp_lcd_draw_pixel (uint32_t x, uint32_t y, uint16_t color)
{
    if ((x < BSP_LCD_WIDTH) && (y < BSP_LCD_HEIGHT))
    {
        g_lcd_fb[0][(y * BSP_LCD_STRIDE) + x] = color;
    }
}

void bsp_lcd_set_backlight (bool on)
{
    R_BSP_PinAccessEnable();
    R_BSP_PinWrite(BSP_LCD_PIN_BL, on);
}

/* ---------------------------------------------------------------- patterns */
void bsp_lcd_status (bsp_lcd_status_t * p_status)
{
    if (NULL == p_status)
    {
        return;
    }

    p_status->bg_en     = R_GLCDC->BG.EN;
    p_status->bg_hsize  = R_GLCDC->BG.HSIZE;
    p_status->bg_vsize  = R_GLCDC->BG.VSIZE;
    p_status->stmon     = R_GLCDC->SYSCNT.STMON;
    p_status->panel_clk = R_GLCDC->SYSCNT.PANEL_CLK;
}

void bsp_lcd_dsi_status (bsp_lcd_dsi_status_t * p_status)
{
    mipi_dsi_status_t dsi_status;

    if (NULL == p_status)
    {
        return;
    }

    p_status->cmd_count  = ra8_mipi_cmd_count();
    p_status->phy_status = ra8_mipi_phy_status();
    p_status->seq0_count = ra8_mipi_seq0_count();

    dsi_status.link_status             = MIPI_DSI_LINK_STATUS_IDLE;
    dsi_status.ack_err_accumulated.bits = 0U;
    dsi_status.ack_err_latest.bits      = 0U;

    (void) R_MIPI_DSI_StatusGet(&g_mipi_dsi0_ctrl, &dsi_status);

    p_status->link_status = (uint32_t) dsi_status.link_status;
    p_status->ack_err     = dsi_status.ack_err_accumulated.bits;
}

#define BSP_LCD_PATTERN_COUNT (5U)

uint32_t bsp_lcd_pattern_count (void)
{
    return BSP_LCD_PATTERN_COUNT;
}

void bsp_lcd_pattern (uint32_t index)
{
    static const uint16_t bars[8] =
    {
        0xF800U, /* red   */
        0xFC00U, /* orange*/
        0xFFE0U, /* yellow*/
        0x07E0U, /* green */
        0x07FFU, /* cyan  */
        0x001FU, /* blue  */
        0x8010U, /* purple*/
        0xFFFFU, /* white */
    };

    switch (index % BSP_LCD_PATTERN_COUNT)
    {
        case 0U: /* black */
            bsp_lcd_fill(0x0000U);
            break;

        case 1U: /* full white */
            bsp_lcd_fill(0xFFFFU);
            break;

        case 2U: /* 8 colour bars */
        {
            uint32_t bw = BSP_LCD_WIDTH / 8U;

            for (uint32_t i = 0; i < 8U; i++)
            {
                bsp_lcd_fill_rect(i * bw, 0, bw, BSP_LCD_HEIGHT, bars[i]);
            }

            break;
        }

        case 3U: /* RGB gradient */
        {
            for (uint32_t y = 0; y < BSP_LCD_HEIGHT; y++)
            {
                for (uint32_t x = 0; x < BSP_LCD_WIDTH; x++)
                {
                    uint8_t r = (uint8_t) ((x * 255U) / (BSP_LCD_WIDTH - 1U));
                    uint8_t g = (uint8_t) ((y * 255U) / (BSP_LCD_HEIGHT - 1U));
                    uint8_t b = (uint8_t) ((x + y) * 255U / (BSP_LCD_WIDTH + BSP_LCD_HEIGHT - 2U));

                    g_lcd_fb[0][(y * BSP_LCD_STRIDE) + x] = rgb888_to_565(r, g, b);
                }
            }

            break;
        }

        case 4U: /* 20 px checkerboard */
        default:
        {
            for (uint32_t y = 0; y < BSP_LCD_HEIGHT; y++)
            {
                for (uint32_t x = 0; x < BSP_LCD_WIDTH; x++)
                {
                    bool on = (0U == (((x / 20U) + (y / 20U)) & 1U));

                    g_lcd_fb[0][(y * BSP_LCD_STRIDE) + x] = on ? 0xFFFFU : 0x0000U;
                }
            }

            break;
        }
    }
}
