/**
  ******************************************************************************
  * @file    usb_descriptors.c
  * @brief   USB descriptors for the CDC-ACM control port.
  *
  *  A single CDC interface pair (control + data) is exposed, which Windows,
  *  Linux and macOS all bind to their built-in ACM driver - no .inf file and
  *  no driver install.  The VID/PID pair below is the well known "TinyUSB
  *  sample" identity; change it before shipping anything real.
  ******************************************************************************
  */
#include "tusb.h"
#include "stm32h7xx.h"      /* UID_BASE: the 96 bit die identifier */

/* 0xCAFE + interface bitmap -> a PID that is stable per feature set, the
 * convention used by the tinyusb examples. */
#define USB_VID     0xCAFE
#define USB_BCD     0x0200
#define USB_PID     (0x4000 | (CFG_TUD_CDC << 0))

/*----------------------------------------------------------------------------
 *  Device descriptor
 *--------------------------------------------------------------------------*/
static const tusb_desc_device_t s_desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    /* Use IAD so the host treats the two interfaces as one composite
     * function; required for CDC on Windows. */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&s_desc_device;
}

/*----------------------------------------------------------------------------
 *  Configuration descriptor
 *--------------------------------------------------------------------------*/
enum
{
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

#define EPNUM_CDC_NOTIF     0x81
#define EPNUM_CDC_OUT       0x02
#define EPNUM_CDC_IN        0x82

static const uint8_t s_desc_fs_configuration[] =
{
    /* config number, interface count, string index, total length,
     * attributes, power in mA */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    /* interface number, string index, EP notification address and size,
     * EP data address (out, in) and size */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_desc_fs_configuration;
}

/*----------------------------------------------------------------------------
 *  String descriptors
 *--------------------------------------------------------------------------*/
enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC_ITF
};

static const char *const s_string_desc[] =
{
    (const char[]){ 0x09, 0x04 },   /* 0: English (0x0409), little endian */
    "LXB",                          /* 1: manufacturer                    */
    "STM32H743 NES Console",        /* 2: product                         */
    NULL,                           /* 3: serial, built from the UID      */
    "NES Control Port",             /* 4: CDC interface                   */
};

static uint16_t s_desc_str[32];

/**
  * @brief  Build the 96 bit unique device ID into an ASCII serial number.
  */
static uint8_t serial_from_uid(char *out, uint8_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    const uint32_t   *uid   = (const uint32_t *)UID_BASE;
    uint8_t           n     = 0U;
    uint8_t           w;
    int8_t            nib;

    for (w = 0U; w < 3U; w++)
    {
        for (nib = 7; nib >= 0; nib--)
        {
            if (n >= cap)
            {
                return n;
            }
            out[n++] = hex[(uid[w] >> (nib * 4)) & 0x0FU];
        }
    }

    return n;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    uint8_t  chr_count = 0U;
    uint8_t  i;
    char     serial[24];
    const char *str;

    (void)langid;

    if (index == STRID_LANGID)
    {
        s_desc_str[1] = 0x0409U;
        chr_count     = 1U;
    }
    else if (index == STRID_SERIAL)
    {
        chr_count = serial_from_uid(serial, (uint8_t)sizeof(serial));
        for (i = 0U; i < chr_count; i++)
        {
            s_desc_str[1U + i] = (uint16_t)serial[i];
        }
    }
    else if (index < (uint8_t)(sizeof(s_string_desc) / sizeof(s_string_desc[0])))
    {
        str = s_string_desc[index];

        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31U)
        {
            chr_count = 31U;
        }

        for (i = 0U; i < chr_count; i++)
        {
            s_desc_str[1U + i] = (uint16_t)str[i];
        }
    }
    else
    {
        return NULL;
    }

    /* First word: length in bytes (header included) + descriptor type. */
    s_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) |
                               (uint16_t)(2U * chr_count + 2U));

    return s_desc_str;
}
