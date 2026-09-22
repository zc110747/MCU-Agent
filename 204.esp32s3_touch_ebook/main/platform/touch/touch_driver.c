/**
 * @file touch_driver.c
 * @brief GT911 bring-up, including the two things this board does differently.
 *
 * 1. RESET IS NOT A GPIO.  The GT911's reset line hangs off CH422G EXIO1, so
 *    the driver's own reset handling cannot be used; it is told the reset pin
 *    is NC and the sequence is performed here instead.
 *
 * 2. THE I2C ADDRESS IS CHOSEN AT POWER-ON.  The controller latches its address
 *    from the level of the interrupt line while reset is released: held LOW it
 *    answers on 0x5D, HIGH on 0x14.  Since we want 0x5D, EXIO1 is pulsed while
 *    GPIO4 is deliberately driven low, and only afterwards is GPIO4 released to
 *    its normal role as the controller's interrupt output.
 *
 * Getting (2) wrong is not a hard failure: the driver simply finds nothing at
 * 0x5D and every touch is silently ignored, which is why touch_init() probes
 * the controller once before reporting success.
 */

#include "touch_driver.h"

#include "board_config.h"
#include "i2c_bus.h"
#include "io_expander.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port_touch.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

/* Reset timing, aligned with the vendor sequence.
 *
 * waveshare_rgb_lcd_port.c (examples/ESP-IDF/09_lvgl_v9_demo/components/)
 * holds RST low for 100 ms, then drives the interrupt line low, waits another
 * 100 ms, and only then releases RST, giving the controller a further 200 ms.
 * The 10 ms this file used before is the datasheet's minimum *pulse* width,
 * not the window a controller needs to come up in. */
#define TOUCH_RST_LOW_MS     (100)   /* vendor: 100 ms */
#define TOUCH_INT_ASSERT_MS  (100)   /* vendor: 100 ms, INT low before RST rises */
#define TOUCH_RST_SETTLE_MS  (200)   /* vendor: 200 ms */
#define TOUCH_INT_RELEASE_MS (100)

static esp_lcd_touch_handle_t s_touch = NULL;
static lv_indev_t *s_indev = NULL;

static esp_err_t int_gpio_mode(gpio_mode_t mode, gpio_pullup_t pull_up)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_IRQ_GPIO,
        .mode = mode,
        .pull_up_en = pull_up,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

/*
 * EXPECTED BOOT WARNING
 * ---------------------
 * `W (909) gpio: conflict found for GPIO[4]` is emitted on purpose and is not a
 * fault.  GPIO4 is configured twice here - once as an output so its level can
 * be held low while the GT911 latches its I2C address, then again as an input
 * to hand the line back to the controller.  The second gpio_config() finds the
 * pin already reserved as an output and says so.
 *
 * esp_driver_gpio/src/gpio.c:401 raises it under
 *     if (esp_gpio_is_reserved(bit_mask) && gpio_hal_input_is_enabled(...))
 * The alternative - one GPIO_MODE_INPUT_OUTPUT config - cannot express what
 * this sequence needs, because the pin has to reach high-Z after the reset
 * pulse rather than be driven high (driving it high would latch 0x14 instead
 * of 0x5D).  Silencing the gpio tag would also hide genuine conflicts, so the
 * warning is left visible and documented instead.
 */

esp_err_t touch_init(void)
{
    if (s_touch != NULL) {
        return ESP_OK;
    }
    if (i2c_bus_handle() == NULL) {
        ESP_LOGE(TAG, "I2C bus is not up; call i2c_bus_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    /* ---- 1. reset, with the interrupt line choosing the I2C address ----
     * 0x5D is latched from the *level* of the interrupt line at the instant RST
     * rises, so that line has to be low and settled before the edge - which is
     * why the vendor holds it low for a full 100 ms first. */
    ESP_RETURN_ON_ERROR(int_gpio_mode(GPIO_MODE_OUTPUT, GPIO_PULLUP_DISABLE), TAG,
                        "IRQ gpio as output failed");
    ESP_RETURN_ON_ERROR(io_expander_touch_reset(false), TAG, "CTP_RST low failed");
    vTaskDelay(pdMS_TO_TICKS(TOUCH_RST_LOW_MS));

    gpio_set_level(BOARD_TOUCH_IRQ_GPIO, 0);          /* -> address 0x5D */
    vTaskDelay(pdMS_TO_TICKS(TOUCH_INT_ASSERT_MS));

    ESP_RETURN_ON_ERROR(io_expander_touch_reset(true), TAG, "CTP_RST high failed");
    vTaskDelay(pdMS_TO_TICKS(TOUCH_RST_SETTLE_MS));

    /* ---- 2. hand the line back to the controller ----------------------
     * As an input, and *with* the internal pull-up: the GT911's interrupt
     * output is open drain, so with the pull-up disabled the line floats
     * whenever the controller is not pulling it down.  (The vendor firmware
     * instead parks this pin as a push-pull output driven low for the rest of
     * the run, which is only safe because that output is open drain - and
     * leaves the controller believing the host is busy forever.) */
    ESP_RETURN_ON_ERROR(int_gpio_mode(GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE), TAG,
                        "IRQ gpio release failed");
    vTaskDelay(pdMS_TO_TICKS(TOUCH_INT_RELEASE_MS));

    /* ---- 3. panel IO + driver ----------------------------------------- */
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_LOGI(TAG, "probing GT911 at 0x%02X on the shared bus", io_cfg.dev_addr);

    esp_lcd_panel_io_handle_t io = NULL;
    esp_err_t err = esp_lcd_new_panel_io_i2c(i2c_bus_handle(), &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_i2c: %s", esp_err_to_name(err));
        return err;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        /* Neither line is owned by the driver: RST is on the expander and IRQ
         * is left as the controller's output, which also keeps LVGL in polling
         * mode instead of needing an ISR. */
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .process_coordinates = NULL,
        .interrupt_callback = NULL,
        .user_data = NULL,
        .driver_data = NULL,
    };

    err = esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_gt911: %s", esp_err_to_name(err));
        s_touch = NULL;
        return err;
    }

    /* ---- 4. prove it actually answers --------------------------------- */
    err = esp_lcd_touch_read_data(s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GT911 did not answer at 0x%02X (%s) - check the FPC and "
                      "the reset/address sequence",
                 io_cfg.dev_addr, esp_err_to_name(err));
        esp_lcd_touch_del(s_touch);
        s_touch = NULL;
        return err;
    }

    ESP_LOGI(TAG, "GT911 ready at 0x%02X  %dx%d  polling mode",
             io_cfg.dev_addr, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

esp_lcd_touch_handle_t touch_handle(void)
{
    return s_touch;
}

lv_indev_t *touch_attach_lvgl(lv_display_t *disp)
{
    if (s_touch == NULL || disp == NULL) {
        return NULL;
    }
    if (s_indev != NULL) {
        return s_indev;
    }

    /* scale is 1:1 because the panel runs at its native resolution and the
     * application never rotates - see board_config.h. */
    const lvgl_port_touch_cfg_t cfg = {
        .disp = disp,
        .handle = s_touch,
        .scale = { .x = 1.0f, .y = 1.0f },
    };

    /* lvgl_port_add_touch() takes the (recursive) LVGL mutex itself. */
    s_indev = lvgl_port_add_touch(&cfg);
    if (s_indev == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        return NULL;
    }

    ESP_LOGI(TAG, "touch registered as an LVGL pointer input device");
    return s_indev;
}
