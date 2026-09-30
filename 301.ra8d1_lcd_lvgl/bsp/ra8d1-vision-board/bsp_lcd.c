/**
 * @file bsp_lcd.c
 * @brief GLCDC driver wiring for the Vision Board RGB 4.3" panel (800x480)
 *
 * Timing / output format taken verbatim from the official BSP generated
 * `ra_gen/common_data.c` (projects/lcd/vision_board_rgb_4.3inch):
 *   800x480 active, htotal 1024 / vtotal 525, sync width 1, back porch 46/23,
 *   sync active low, DE active high, sync on rising edge, RGB666 output.
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
#include "r_glcdc.h"

/* Panel control lines. See documents/pinmap.md and bsp_pin.c. */
#define BSP_LCD_PIN_BL       BSP_IO_PORT_10_PIN_11
#define BSP_LCD_PIN_RESET    BSP_IO_PORT_11_PIN_04

#define BSP_LCD_LAYER        (0)

/* -------------------------------------------------------------- framebuffer */
/* .sdram is a NOLOAD section, so this costs no flash and no internal RAM. */
static uint16_t g_lcd_fb[BSP_LCD_STRIDE * BSP_LCD_HEIGHT]
    BSP_ALIGN_VARIABLE(64) BSP_PLACE_IN_SECTION(".sdram");

/* ------------------------------------------------------------ GLCDC objects */
static glcdc_instance_ctrl_t g_display_ctrl;

static const glcdc_extended_cfg_t g_display_extend_cfg =
{
    .tcon_hsync            = GLCDC_TCON_PIN_0,
    .tcon_vsync            = GLCDC_TCON_PIN_1,
    .tcon_de               = GLCDC_TCON_PIN_3,
    .correction_proc_order = GLCDC_CORRECTION_PROC_ORDER_BRIGHTNESS_CONTRAST2GAMMA,
    .clksrc                = GLCDC_CLK_SRC_INTERNAL,
    .clock_div_ratio       = GLCDC_PANEL_CLK_DIVISOR_8,
    .dithering_mode        = GLCDC_DITHERING_MODE_TRUNCATE,
    .dithering_pattern_A   = GLCDC_DITHERING_PATTERN_11,
    .dithering_pattern_B   = GLCDC_DITHERING_PATTERN_11,
    .dithering_pattern_C   = GLCDC_DITHERING_PATTERN_11,
    .dithering_pattern_D   = GLCDC_DITHERING_PATTERN_11,
    .phy_layer             = NULL,
};

static const display_cfg_t g_display_cfg =
{
    /* Graphics layer 1 (background) - the only layer Phase 2 uses. */
    .input[0] =
    {
        .p_base                  = (uint32_t *) &g_lcd_fb[0],
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

    .output =
    {
        .htiming =
        {
            .total_cyc          = 1024,
            .display_cyc        = 800,
            .back_porch         = 46,
            .sync_width         = 1,
            .sync_polarity      = DISPLAY_SIGNAL_POLARITY_LOACTIVE,
        },
        .vtiming =
        {
            .total_cyc          = 525,
            .display_cyc        = 480,
            .back_porch         = 23,
            .sync_width         = 1,
            .sync_polarity      = DISPLAY_SIGNAL_POLARITY_LOACTIVE,
        },
        .format                = DISPLAY_OUT_FORMAT_18BITS_RGB666,
        .endian                = DISPLAY_ENDIAN_BIG,
        .color_order           = DISPLAY_COLOR_ORDER_RGB,
        .data_enable_polarity  = DISPLAY_SIGNAL_POLARITY_HIACTIVE,
        .sync_edge             = DISPLAY_SIGNAL_SYNC_EDGE_RISING,
        .bg_color              = {.byte = {.a = 255, .r = 0, .g = 0, .b = 0}},
        .brightness            = {.enable = false, .r = 512, .g = 512, .b = 512},
        .contrast              = {.enable = false, .r = 128, .g = 128, .b = 128},
        .p_gamma_correction    = NULL,
        .dithering_on          = false,
    },

    /* No interrupts: no vsync callback in Phase 2, so every vector is invalid. */
    .line_detect_ipl         = BSP_IRQ_DISABLED,
    .underflow_1_ipl         = BSP_IRQ_DISABLED,
    .underflow_2_ipl         = BSP_IRQ_DISABLED,
    .line_detect_irq         = FSP_INVALID_VECTOR,
    .underflow_1_irq         = FSP_INVALID_VECTOR,
    .underflow_2_irq         = FSP_INVALID_VECTOR,

    .p_callback              = NULL,
    .p_context               = NULL,
    .p_extend                = &g_display_extend_cfg,
};

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

    err = R_GLCDC_Open(&g_display_ctrl, &g_display_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = R_GLCDC_Start(&g_display_ctrl);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* No interrupts are wired in Phase 2, so nothing clears the status flags
       for us. Clear them once here: from now on a set L1UNDF bit means the
       GLCDC really failed to fetch layer 1 from SDRAM.

       NOTE: STMON.L2UNDF reads 1 permanently and re-asserts ~1-2 frame periods
       after a STCLR clear (measured over SWD, tools/verify/probe_stmon.py).
       This is a benign hardware artifact: the FSP driver arms the GR[1]
       line-detect position unconditionally (r_glcdc.c CLUTINT_b.LINE) while
       layer 2 is transparent with FLMRD.RENB=0 (no memory fetch). The sticky
       GR[1].MON.UNDFLST bit then mirrors into STMON.L2UNDF. Layer 1 -- the
       only layer that fetches -- never underflows (L1UNDF=0, GR[0].MON=0),
       so acceptance checks L1UNDF only. */
    R_GLCDC->SYSCNT.STCLR_b.VPOSCLR   = 1U;
    R_GLCDC->SYSCNT.STCLR_b.L1UNDFCLR = 1U;
    R_GLCDC->SYSCNT.STCLR_b.L2UNDFCLR = 1U;

    bsp_lcd_fill(rgb888_to_565(0, 0, 0));
    bsp_lcd_set_backlight(true);

    return FSP_SUCCESS;
}

uint16_t * bsp_lcd_framebuffer (void)
{
    return &g_lcd_fb[0];
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
        uint16_t * p = &g_lcd_fb[((y + row) * BSP_LCD_STRIDE) + x];

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
        g_lcd_fb[(y * BSP_LCD_STRIDE) + x] = color;
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

                    g_lcd_fb[(y * BSP_LCD_STRIDE) + x] = rgb888_to_565(r, g, b);
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

                    g_lcd_fb[(y * BSP_LCD_STRIDE) + x] = on ? 0xFFFFU : 0x0000U;
                }
            }

            break;
        }
    }
}
