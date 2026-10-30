/**
 ******************************************************************************
 * @file    usb_descriptors.c
 * @brief   USB descriptors for the composite-free HID mouse device.
 *
 *  Report layout (3 bytes, no Report ID so the device also works from a BIOS):
 *
 *      byte 0  buttons   bit0 = left, bit1 = right, bit2 = middle
 *      byte 1  X         signed, relative, -127 .. +127
 *      byte 2  Y         signed, relative, -127 .. +127
 *
 *  There is deliberately no wheel byte: scrolling is out of scope for now.
 *  Adding one later only means appending a 4th byte to the descriptor and to
 *  the report written by usb_mouse.c.
 ******************************************************************************
 */
#include <string.h>

#include "tusb.h"

/* ---- Device identity ------------------------------------------------------ */
#define USB_VID               0x0483        /* STMicroelectronics            */
#define USB_PID               0x5720        /* HID mouse demo                */
#define USB_BCD               0x0100

/* ---- Endpoints ------------------------------------------------------------ */
#define EPNUM_HID             0x81          /* IN, interrupt                 */

/* ---- String descriptors --------------------------------------------------- */
enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL
};

static char const *string_desc_arr[] =
{
    (const char[]){ 0x09, 0x04 },           /* 0: en-US (filled in below)    */
    "MCU-Agent",                            /* 1: manufacturer               */
    "STM32F429 Touchpad Mouse",             /* 2: product                    */
    "105000000001",                         /* 3: serial                     */
};

/* ---- HID report descriptor ------------------------------------------------ */
static uint8_t const desc_hid_report[] =
{
    0x05, 0x01,        /* Usage Page (Generic Desktop)                     */
    0x09, 0x02,        /* Usage (Mouse)                                    */
    0xA1, 0x01,        /* Collection (Application)                         */
    0x09, 0x01,        /*   Usage (Pointer)                                */
    0xA1, 0x00,        /*   Collection (Physical)                          */
    0x05, 0x09,        /*     Usage Page (Button)                          */
    0x19, 0x01,        /*     Usage Minimum (1)                            */
    0x29, 0x03,        /*     Usage Maximum (3)                            */
    0x15, 0x00,        /*     Logical Minimum (0)                          */
    0x25, 0x01,        /*     Logical Maximum (1)                          */
    0x95, 0x03,        /*     Report Count (3)                             */
    0x75, 0x01,        /*     Report Size (1)                              */
    0x81, 0x02,        /*     Input (Data, Var, Abs)                        */
    0x95, 0x01,        /*     Report Count (1)                             */
    0x75, 0x05,        /*     Report Size (5)                              */
    0x81, 0x03,        /*     Input (Const, Var, Abs)  <- 1 byte padding    */
    0x05, 0x01,        /*     Usage Page (Generic Desktop)                 */
    0x09, 0x30,        /*     Usage (X)                                    */
    0x09, 0x31,        /*     Usage (Y)                                    */
    0x15, 0x81,        /*     Logical Minimum (-127)                       */
    0x25, 0x7F,        /*     Logical Maximum (127)                        */
    0x75, 0x08,        /*     Report Size (8)                              */
    0x95, 0x02,        /*     Report Count (2)                             */
    0x81, 0x06,        /*     Input (Data, Var, Rel)                       */
    0xC0,              /*   End Collection                                 */
    0xC0               /* End Collection                                   */
};

/* ---- Device descriptor ---------------------------------------------------- */
static tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,             /* per interface                */
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = USB_BCD,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 0x01
};

/* ---- Configuration descriptor -------------------------------------------- */
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static uint8_t const desc_configuration[] =
{
    /* TinyUSB 0.21 signature is (config_num, itf_count, str_idx, total_len,
     * attribute, power_ma) - config_num comes FIRST, unlike older releases.
     * Getting this backwards declares a configuration with zero interfaces,
     * which Windows answers with "device cannot start (code 10)" without ever
     * requesting the HID report descriptor. */
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0, 100U),

    /* interface, string index, boot protocol, report length, endpoint */
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE, 8)
};

/* ---- Callbacks ------------------------------------------------------------ */
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    /* One static buffer is enough: the stack copies it out before the next
     * request.  31 characters + length prefix = 64 bytes. */
    static uint16_t desc_str[32];
    uint8_t chr_count;

    (void)langid;

    if (index == STRID_LANGID)
    {
        desc_str[1] = 0x0409;               /* en-US                       */
        chr_count   = 1;
    }
    else
    {
        size_t i;

        if (index >= (uint8_t)(sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
        {
            return NULL;
        }

        const char *str = string_desc_arr[index];

        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31U)
        {
            chr_count = 31U;
        }

        for (i = 0U; i < chr_count; i++)
        {
            desc_str[1U + i] = (uint16_t)str[i];
        }
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2U * chr_count + 2U));
    return desc_str;
}

/* ---- HID class callbacks -------------------------------------------------- */
/* TinyUSB 0.21 declares these as strong references, so they must exist even
 * though a plain mouse has nothing to answer: the only SET_REPORT a host sends
 * is the keyboard LED state, and GET_REPORT is never used by a mouse driver. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0U;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}
