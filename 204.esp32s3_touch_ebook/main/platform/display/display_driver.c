#include "display_driver.h"
#include "io_expander.h"

#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"      /* esp_lcd_panel_reset/init + draw_bitmap */
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

#define LCD_RST_LOW_MS   (20)
#define LCD_RST_SETTLE_MS (120)

static esp_lcd_panel_handle_t s_panel = NULL;
static bool s_backlight_on = false;

static void fill_data_gpio_nums(gpio_num_t *dst)
{
    /* Index 0 is the *least* significant bit of the 16-bit pixel value.
     * ESP-IDF stores the frame buffer as RGB565, therefore:
     *    bit 15..11 = Red[4:0]   -> panel R7..R3
     *    bit 10.. 5 = Green[5:0] -> panel G7..G2
     *    bit  4.. 0 = Blue[4:0]  -> panel B7..B3
     * which is exactly how the board is wired (see board_config.h).          */
    dst[0]  = BOARD_LCD_B3_GPIO;
    dst[1]  = BOARD_LCD_B4_GPIO;
    dst[2]  = BOARD_LCD_B5_GPIO;
    dst[3]  = BOARD_LCD_B6_GPIO;
    dst[4]  = BOARD_LCD_B7_GPIO;
    dst[5]  = BOARD_LCD_G2_GPIO;
    dst[6]  = BOARD_LCD_G3_GPIO;
    dst[7]  = BOARD_LCD_G4_GPIO;
    dst[8]  = BOARD_LCD_G5_GPIO;
    dst[9]  = BOARD_LCD_G6_GPIO;
    dst[10] = BOARD_LCD_G7_GPIO;
    dst[11] = BOARD_LCD_R3_GPIO;
    dst[12] = BOARD_LCD_R4_GPIO;
    dst[13] = BOARD_LCD_R5_GPIO;
    dst[14] = BOARD_LCD_R6_GPIO;
    dst[15] = BOARD_LCD_R7_GPIO;
}

esp_err_t display_init(void)
{
    if (s_panel) {
        return ESP_OK;
    }

    /* --- 1. hardware reset through the CH422G --------------------------- */
    ESP_RETURN_ON_ERROR(io_expander_lcd_reset(false), TAG, "LCD_RST low failed");
    vTaskDelay(pdMS_TO_TICKS(LCD_RST_LOW_MS));
    ESP_RETURN_ON_ERROR(io_expander_lcd_reset(true), TAG, "LCD_RST high failed");
    vTaskDelay(pdMS_TO_TICKS(LCD_RST_SETTLE_MS));

    /* --- 2. RGB panel --------------------------------------------------- */
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = BOARD_LCD_PCLK_HZ,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT_PORCH,
            .flags = {
                .hsync_idle_low = 0,
                .vsync_idle_low = 0,
                .de_idle_high = 0,
                .pclk_active_neg = 0,
                .pclk_idle_high = 0,
            },
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = BOARD_LCD_NUM_FB,
        .bounce_buffer_size_px = 0,
        .dma_burst_size = 64,
        .hsync_gpio_num = BOARD_LCD_HSYNC_GPIO,
        .vsync_gpio_num = BOARD_LCD_VSYNC_GPIO,
        .de_gpio_num = BOARD_LCD_DE_GPIO,
        .pclk_gpio_num = BOARD_LCD_PCLK_GPIO,
        .disp_gpio_num = GPIO_NUM_NC,   /* backlight is on the expander */
        .data_gpio_nums = {0},
        .flags = {
            .disp_active_low = 0,
            .refresh_on_demand = 0,
            .fb_in_psram = 1,
            .double_fb = 0,
            .no_fb = 0,
            .bb_invalidate_cache = 0,
        },
    };
    fill_data_gpio_nums(cfg.data_gpio_nums);

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel: %s", esp_err_to_name(err));
        s_panel = NULL;
        return err;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init failed");

    /* --- 3. paint both frame buffers black before the backlight comes up -- */
    void *fb[BOARD_LCD_NUM_FB] = {NULL};
    if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, BOARD_LCD_NUM_FB, &fb[0], &fb[1]) == ESP_OK) {
        const size_t bytes = (size_t)BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t);
        for (size_t i = 0; i < BOARD_LCD_NUM_FB; ++i) {
            if (fb[i]) {
                memset(fb[i], 0x00, bytes);
            }
        }
    }

    ESP_LOGI(TAG, "RGB panel ready %dx%d  pclk=%uMHz  fb=%d in PSRAM",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES,
             (unsigned)(BOARD_LCD_PCLK_HZ / 1000000), BOARD_LCD_NUM_FB);
    return ESP_OK;
}

esp_lcd_panel_handle_t display_panel(void)
{
    return s_panel;
}

esp_err_t display_backlight_set(bool on)
{
    esp_err_t err = io_expander_backlight_enable(on);
    if (err == ESP_OK) {
        s_backlight_on = on;
    }
    return err;
}

bool display_backlight_get(void)
{
    return s_backlight_on;
}
