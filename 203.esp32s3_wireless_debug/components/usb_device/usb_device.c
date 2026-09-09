/**
 * @file usb_device.c
 * @brief TinyUSB device lifecycle + HID data path.
 *
 * Flow:  USB RX (HID OUT) -> rx queue -> DAP task -> response -> HID IN
 * The USB task runs tud_task(); DAP processing happens in cmsis_dap's task.
 */
#include "usb_device.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_private/usb_phy.h"
#include "tusb.h"
#include "esp_log.h"

static const char *TAG = "usb";

static QueueHandle_t s_rx_queue = NULL;
static SemaphoreHandle_t s_tx_done = NULL;
static usb_phy_handle_t s_phy_handle = NULL;

/* ------------------------------------------------------------------ */
/* TinyUSB callbacks (run in USB task context)                         */
/* ------------------------------------------------------------------ */

/* HID OUT report from host -> DAP request */
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)itf;
    (void)report_id;

    /* TinyUSB delivers host->device data via this callback for both
     * interrupt OUT (report_type = OUTPUT/INVALID) and SET_REPORT control
     * (report_type = FEATURE). Do NOT gate on a specific report_type,
     * otherwise interrupt-OUT commands get silently dropped and the host
     * sees no response (e.g. OpenOCD "CMD_INFO failed").
     * NOTE: this runs on EVERY HID packet; keep it at VERBOSE so the
     * serial log never becomes the download-speed bottleneck. */
    ESP_LOGV(TAG, "SET_REPORT type=%u id=%u len=%u b0=%02X b1=%02X",
             (unsigned)report_type, (unsigned)report_id, (unsigned)bufsize,
             buffer ? buffer[0] : 0, buffer ? buffer[1] : 0);
    if (bufsize > 0 && buffer != NULL) {
        usb_dap_msg_t msg = { .len = 0 };
        msg.len = (bufsize > USB_DAP_PACKET_SIZE) ? USB_DAP_PACKET_SIZE : bufsize;
        memcpy(msg.data, buffer, msg.len);
        ESP_LOGV(TAG, "HID OUT cmd=0x%02X len=%u", buffer[0], (unsigned)msg.len);
        if (s_rx_queue) {
            /* NEVER drop DAP commands silently: OpenOCD pipelines many HID OUT
             * reports (JTAG/SWD flash download bursts). A dropped command means
             * the host waits for a response that never comes -> timeout + retry
             * = multi-second stalls. Queue is deep enough to absorb a full
             * OpenOCD command batch; if it ever overflows, complain loudly. */
            if (xQueueSend(s_rx_queue, &msg, 0) != pdTRUE) {
                static uint32_t s_dropped;
                s_dropped++;
                ESP_LOGE(TAG, "rx queue FULL, DAP cmd 0x%02X DROPPED (total %u)",
                         buffer[0], (unsigned)s_dropped);
            }
        }
    }
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)itf; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_report_complete_cb(uint8_t itf, uint8_t const *report, uint16_t len)
{
    (void)itf; (void)report; (void)len;
    ESP_LOGV(TAG, "RESP complete itf=%u len=%u", (unsigned)itf, (unsigned)len);
    if (s_tx_done) {
        xSemaphoreGive(s_tx_done);
    }
}

/* Optional: log USB events */
void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB configured");
}

void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "USB unmounted");
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
QueueHandle_t usb_device_rx_queue(void)
{
    return s_rx_queue;
}

esp_err_t usb_device_send_response(const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!tud_connected()) {
        ESP_LOGW(TAG, "RESP: not connected");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGV(TAG, "RESP payload=%u b0=%02X b1=%02X b2=%02X",
             (unsigned)len, data[0], data[1], data[2]);

    /* CMSIS-DAP v1 host tools (OpenOCD / Keil) read a FULL 64-byte report per
     * command and use response[1] as the payload length. Sending a short
     * packet makes the host block until its read timeout -> "CMD_INFO failed".
     * Always transmit the full packet; the caller's buffer is zero-padded to
     * USB_DAP_PACKET_SIZE so trailing bytes are 0. This mirrors the reference
     * STM32H7 implementation: tud_hid_report(0, resp_buf, DAP_PACKET_SIZE). */
    for (;;) {
        if (tud_hid_report(0, data, USB_DAP_PACKET_SIZE)) {
            ESP_LOGV(TAG, "RESP sent ok (full %u)", (unsigned)USB_DAP_PACKET_SIZE);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "RESP tud_hid_report busy, retry");
        if (xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(100)) != pdTRUE) {
            /* still busy - retry; host may have stalled the endpoint */
            if (!tud_ready()) {
                ESP_LOGW(TAG, "RESP abort: not ready");
                return ESP_ERR_INVALID_STATE;
            }
        }
    }
}

static void usb_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "USB task running");
    for (;;) {
        tud_task();
    }
}

esp_err_t usb_device_init(void)
{
    /* Internal USB OTG PHY routed to GPIO19/20 */
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_UNDEFINED,   /* auto (FS) */
    };
    esp_err_t err = usb_new_phy(&phy_conf, &s_phy_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_new_phy failed: %s", esp_err_to_name(err));
        return err;
    }

    s_rx_queue = xQueueCreate(32, sizeof(usb_dap_msg_t));
    s_tx_done = xSemaphoreCreateBinary();
    if (s_rx_queue == NULL || s_tx_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    if (!tusb_rhport_init(0, &dev_init)) {
        ESP_LOGE(TAG, "tusb_rhport_init failed");
        return ESP_FAIL;
    }

    if (xTaskCreate(usb_task, "usb_task", 4096, NULL, 10, NULL) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
