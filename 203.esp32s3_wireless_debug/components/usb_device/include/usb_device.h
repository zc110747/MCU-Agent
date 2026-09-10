/**
 * @file usb_device.h
 * @brief USB device layer: TinyUSB HID carrying CMSIS-DAP packets.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_DAP_PACKET_SIZE 64

typedef struct {
    uint8_t data[USB_DAP_PACKET_SIZE];
    uint16_t len;
} usb_dap_msg_t;

/**
 * @brief Init USB PHY + TinyUSB device stack + USB task.
 *        Creates the CMSIS-DAP request queue.
 */
esp_err_t usb_device_init(void);

/**
 * @brief Queue receiving DAP requests from the host (HID OUT reports).
 */
QueueHandle_t usb_device_rx_queue(void);

/**
 * @brief True only when a USB host has fully ENUMERATED the device (SET_CONFIG
 *        done, i.e. tud_mounted()).
 *
 * Used for transport arbitration: the probe must NOT start the Wi-Fi SoftAP
 * while a USB host is present, because Wi-Fi RF activity on ESP32-S3 breaks
 * the USB OTG HID transfer timing.
 *
 * Do NOT use tud_connected() for this: on ESP32-S3 it only means "VBUS
 * present", so a probe plugged into a plain charger / power bank would be
 * misdetected as "USB host attached" and the SoftAP would never start.
 */
bool usb_device_is_connected(void);

/**
 * @brief True when VBUS is present (cable plugged in), host or not.
 *        Diagnostic counterpart of usb_device_is_connected().
 */
bool usb_device_vbus_present(void);

/**
 * @brief Send a DAP response to the host (HID IN report, blocks until sent).
 */
esp_err_t usb_device_send_response(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
