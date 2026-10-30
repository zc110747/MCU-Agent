/* ---------------------------------------------------------------------------
 * usb_descriptors.c - standard USB HID mouse.
 *
 * Report layout (5 bytes, no Report ID):
 *
 *   byte 0  buttons   bit0 = left, bit1 = right, bit2 = middle
 *   byte 1  x         signed, -127..127, relative, + = right
 *   byte 2  y         signed, -127..127, relative, + = down
 *   byte 3  wheel     signed, -127..127, relative, + = away from the user
 *   byte 4  pan       signed, -127..127, relative, + = right   (reserved)
 *
 * Why no Report ID: with a single report there is nothing to disambiguate, the
 * payload stays at 5 bytes, and every host-side Raw Input sample that assumes
 * "a mouse report is the payload" works unchanged. See docs/protocol.md.
 *
 * Why the interface is declared HID_ITF_PROTOCOL_NONE: a boot-protocol mouse
 * report is exactly 3 bytes (buttons/x/y). This descriptor is 5 bytes, so
 * advertising boot compatibility would be a lie the BIOS could act on. The
 * device therefore only ever runs in report protocol - which is all Windows,
 * Linux and macOS use.
 * -------------------------------------------------------------------------*/

#include "tusb.h"
#include "stm32h7xx.h"
#include "usb_desc_defs.h"
#include "usb_descriptors.h"
#include "mouse_report.h"

#define USB_LANGID_ENGLISH   0x0409u

enum
{
    STRID_LANGID = 0,
    STRID_MANUF,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_HID_ITF
};

/* ------------------------------------------------------------------------ */
static const uint8_t desc_hid_report[] =
{
    0x05, 0x01,        /* Usage Page (Generic Desktop)                  */
    0x09, 0x02,        /* Usage (Mouse)                                 */
    0xA1, 0x01,        /* Collection (Application)                      */
    0x09, 0x01,        /*   Usage (Pointer)                             */
    0xA1, 0x00,        /*   Collection (Physical)                       */

    0x05, 0x09,        /*     Usage Page (Button)                       */
    0x19, 0x01,        /*     Usage Minimum (Button 1)                  */
    0x29, 0x03,        /*     Usage Maximum (Button 3)                  */
    0x15, 0x00,        /*     Logical Minimum (0)                       */
    0x25, 0x01,        /*     Logical Maximum (1)                       */
    0x95, 0x03,        /*     Report Count (3)                          */
    0x75, 0x01,        /*     Report Size (1)                           */
    0x81, 0x02,        /*     Input (Data, Variable, Absolute)          */
    0x95, 0x01,        /*     Report Count (1)                          */
    0x75, 0x05,        /*     Report Size (5) - pad to a whole byte     */
    0x81, 0x03,        /*     Input (Constant, Variable, Absolute)      */

    0x05, 0x01,        /*     Usage Page (Generic Desktop)              */
    0x09, 0x30,        /*     Usage (X)                                 */
    0x09, 0x31,        /*     Usage (Y)                                 */
    0x09, 0x38,        /*     Usage (Wheel)                             */
    0x15, 0x81,        /*     Logical Minimum (-127)                    */
    0x25, 0x7F,        /*     Logical Maximum (127)                     */
    0x75, 0x08,        /*     Report Size (8)                           */
    0x95, 0x03,        /*     Report Count (3)                          */
    0x81, 0x06,        /*     Input (Data, Variable, Relative)          */

    0x05, 0x0C,        /*     Usage Page (Consumer)                     */
    0x0A, 0x38, 0x02,  /*     Usage (AC Pan) - horizontal wheel         */
    0x15, 0x81,        /*     Logical Minimum (-127)                    */
    0x25, 0x7F,        /*     Logical Maximum (127)                     */
    0x75, 0x08,        /*     Report Size (8)                           */
    0x95, 0x01,        /*     Report Count (1)                          */
    0x81, 0x06,        /*     Input (Data, Variable, Relative)          */

    0xC0,              /*   End Collection (Physical)                   */
    0xC0               /* End Collection (Application)                  */
};

/* The descriptor above must agree with mouse_report_pack(). 3 button bits are
 * padded to one byte, then 4 signed bytes = 5. */
_Static_assert(sizeof(desc_hid_report) == 67u,
               "HID report descriptor size changed - re-check the layout comments");
_Static_assert(MOUSE_REPORT_WIRE_LEN == 5u, "wire length drift");

/* ------------------------------------------------------------------------ */
static const tusb_desc_device_t desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    /* Class is defined at the interface level, not here. */
    .bDeviceClass       = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass    = 0x00u,
    .bDeviceProtocol    = 0x00u,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100u,

    .iManufacturer      = STRID_MANUF,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,

    .bNumConfigurations = 0x01u
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *) &desc_device;
}

/* ------------------------------------------------------------------------ */
#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t desc_configuration[CONFIG_TOTAL_LEN] =
{
    TUD_CONFIG_DESCRIPTOR(1, USB_ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(USB_ITF_NUM_HID, STRID_HID_ITF, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), USB_EPNUM_HID_IN,
                       USB_HID_EP_SIZE, USB_HID_POLL_MS)
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;
    return desc_configuration;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void) instance;
    return desc_hid_report;
}

uint16_t usb_descriptors_report_len(void)
{
    return (uint16_t) sizeof(desc_hid_report);
}

/* ------------------------------------------------------------------------ */
static const uint16_t desc_string_langid[2] =
{
    (uint16_t) ((TUSB_DESC_STRING << 8) | (2u * 2u + 2u)),
    USB_LANGID_ENGLISH
};

/* The STM32 factory UID is 96 bits at 0x1FF1E800. Deriving the serial from it
 * lets the host tool tell two boards apart. */
static void uid_to_hex(char *out, uint32_t out_len)
{
    const uint32_t *uid = (const uint32_t *) UID_BASE;
    static const char hex[] = "0123456789ABCDEF";
    uint32_t o = 0u;

    for (uint32_t w = 0u; w < 3u; w++)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            if ((o + 1u) >= out_len)
            {
                break;
            }
            out[o] = hex[(uid[w] >> shift) & 0x0Fu];
            o++;
        }
    }
    out[o] = '\0';
}

#define STRBUF_WORDS   40u
static uint16_t s_strbuf[STRBUF_WORDS];

static uint16_t *strbuf_from_ascii(const char *s)
{
    uint32_t len = 0u;

    while ((s[len] != '\0') && (len < (STRBUF_WORDS - 1u)))
    {
        len++;
    }

    s_strbuf[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (uint16_t) (2u * len + 2u));
    for (uint32_t i = 0u; i < len; i++)
    {
        s_strbuf[1u + i] = (uint16_t) (uint8_t) s[i];
    }

    return s_strbuf;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static char serial[25];

    (void) langid;

    switch (index)
    {
        case STRID_LANGID:
            return desc_string_langid;

        case STRID_MANUF:
            return strbuf_from_ascii(USB_MANUF_STR);

        case STRID_PRODUCT:
            return strbuf_from_ascii(USB_PRODUCT_STR);

        case STRID_SERIAL:
            uid_to_hex(serial, sizeof(serial));
            return strbuf_from_ascii(serial);

        case STRID_HID_ITF:
            return strbuf_from_ascii("HID Mouse");

        default:
            return 0;
    }
}
