/**
 * @file wifi_dap.h
 * @brief CMSIS-DAP over WiFi (TCP) transport — minimal Phase-1 extension.
 *
 * This component adds a wireless CMSIS-DAP transport WITHOUT touching the
 * existing USB HID path or the SWD/JTAG/command-handler cores. It reuses the
 * public DAP core entry point cmsis_dap_execute() plus the debug_engine lock
 * (the lock header already documents "Wi-Fi tomorrow also holds this lock").
 *
 * Wire framing (why a header is needed):
 *   USB HID preserves packet boundaries, so the existing DAP core expects one
 *   complete CMSIS-DAP command per HID report. TCP is a byte stream and DAP
 *   commands are variable length, so we prepend a small length-prefixed header
 *   to delimit each request/response. This mirrors the framing used by the
 *   reference design (bkuschak/cmsis_dap_tcp_esp32) and avoids "command
 *   mismatch" errors that arise from stream re-segmentation.
 *
 * Note on OpenOCD compatibility:
 *   Stock OpenOCD (incl. Sysprogs 0.12.0) has NO cmsis-dap TCP backend. To use
 *   this transport with OpenOCD you must build OpenOCD from source with the
 *   tcp backend, or use a client that speaks this framing (see
 *   tools/wifi_dap_client.py). The firmware side is backend-agnostic.
 */
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** TCP port the CMSIS-DAP server listens on (per project spec). */
#define WIFI_DAP_TCP_PORT 50000

/** Signature word ("DAP\0") in little-endian on the wire. */
#define CMSIS_DAP_TCP_HDR_SIGNATURE 0x00504144U

/** Packet type field values. */
#define CMSIS_DAP_TCP_PKT_REQUEST  0x01U
#define CMSIS_DAP_TCP_PKT_RESPONSE 0x02U

/** Maximum CMSIS-DAP payload size (64 B aligns with the HID v1 report size). */
#define CMSIS_DAP_TCP_MAX_PAYLOAD  64U

/** Frame header prepended to every TCP message (wire format: little-endian). */
typedef struct __attribute__((packed)) {
    uint32_t signature;   /**< Must equal CMSIS_DAP_TCP_HDR_SIGNATURE */
    uint16_t length;      /**< Payload length, EXCLUDING this header */
    uint8_t  packet_type; /**< CMSIS_DAP_TCP_PKT_REQUEST / _RESPONSE */
    uint8_t  reserved;    /**< Reserved, must be 0 */
} cmsis_dap_tcp_packet_hdr_t;

/** Size of the wire header. */
#define CMSIS_DAP_TCP_HDR_SIZE sizeof(cmsis_dap_tcp_packet_hdr_t)

/**
 * @brief Initialize the wireless CMSIS-DAP transport.
 *
 * Starts a SoftAP (SSID ESP32-DAP-XXXX, IP 192.168.4.1) and launches the
 * TCP server task listening on WIFI_DAP_TCP_PORT. Safe to call after
 * cmsis_dap_init() has set up the shared DAP core.
 *
 * @return ESP_OK on success, else an esp_err_t code.
 */
esp_err_t wifi_dap_init(void);

#ifdef __cplusplus
}
#endif
