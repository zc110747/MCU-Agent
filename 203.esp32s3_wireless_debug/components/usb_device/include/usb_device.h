/**
 * @file usb_device.h
 * @brief USB device layer: TinyUSB HID carrying CMSIS-DAP packets.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
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
 * @brief Send a DAP response to the host (HID IN report, blocks until sent).
 */
esp_err_t usb_device_send_response(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
