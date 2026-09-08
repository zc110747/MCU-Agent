/**
 * @file usb_descriptors.c
 * @brief TinyUSB device descriptors for CMSIS-DAP v1 (HID).
 *
 * All identity strings come from Kconfig; the serial number is derived from
 * the ESP32-S3 eFuse MAC (never hardcoded).
 */
#include "tusb.h"
#include "usb_device.h"
#include "esp_mac.h"
#include "sdkconfig.h"
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Device descriptor                                                   */
/* ------------------------------------------------------------------ */
static const tusb_desc_device_t s_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = CONFIG_DEBUG_PROBE_USB_VID,
    .idProduct          = CONFIG_DEBUG_PROBE_USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_desc_device;
}

/* ------------------------------------------------------------------ */
/* HID report descriptor: generic in/out, 64-byte vendor reports       */
/* ------------------------------------------------------------------ */
static const uint8_t s_hid_report_desc[] = {
    TUD_HID_REPORT_DESC_GENERIC_INOUT(CFG_TUD_ENDPOINT0_SIZE)
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf)
{
    (void)itf;
    return s_hid_report_desc;
}

/* ------------------------------------------------------------------ */
/* Configuration descriptor: config + HID interface + 2 endpoints      */
/* ------------------------------------------------------------------ */
enum {
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const uint8_t s_desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    /* Interface 0: HID, no boot protocol, 2 interrupt endpoints */
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 4, HID_ITF_PROTOCOL_NONE,
                             sizeof(s_hid_report_desc),
                             0x01, 0x81, CFG_TUD_ENDPOINT0_SIZE, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_desc_configuration;
}

/* ------------------------------------------------------------------ */
/* String descriptors                                                  */
/* ------------------------------------------------------------------ */
static char s_serial_str[2 * 6 + 1];  /* 12 hex digits + NUL */

static const char *const s_string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },  /* 0: supported language (English 0x0409) */
    CONFIG_DEBUG_PROBE_USB_MANUFACTURER,    /* 1 */
    CONFIG_DEBUG_PROBE_USB_PRODUCT,         /* 2 */
    s_serial_str,                           /* 3 */
    "CMSIS-DAP",                            /* 4: interface string (host match) */
};

static uint16_t ascii_to_utf16le(const char *str, uint16_t *buf, uint16_t bufsize)
{
    uint16_t n = 0;
    while (*str && (2 + 2 * n + 2) <= bufsize) {
        buf[1 + n++] = (uint8_t)(*str++);
    }
    buf[0] = (uint16_t)(0x0300 | (2 * n + 2));
    return 2 * n + 2;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    static uint16_t desc_str[32 + 1];

    if (index == 0) {
        desc_str[1] = 0x0409;   /* English (US) */
        desc_str[0] = 0x0304;   /* 4 bytes total */
        return desc_str;
    }

    if (s_serial_str[0] == '\0') {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);
        snprintf(s_serial_str, sizeof(s_serial_str),
                 "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    if (index >= sizeof(s_string_desc_arr) / sizeof(s_string_desc_arr[0])) {
        return NULL;
    }

    const char *str = s_string_desc_arr[index];
    if (ascii_to_utf16le(str, desc_str, sizeof(desc_str)) == 0) {
        return NULL;
    }
    return desc_str;
}
