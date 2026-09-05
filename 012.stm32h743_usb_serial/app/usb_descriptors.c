/* ---------------------------------------------------------------------------
 * USB descriptors - single CDC-ACM (virtual COM port).
 *
 * The device advertises Miscellaneous / Common / IAD at device level. That is
 * what makes Windows bind its in-box usbser.sys to the CDC function with no
 * .inf file, and Linux/macOS pick it up as /dev/ttyACM* / cu.usbmodem*.
 *
 * The serial number is derived from the 96-bit STM32 unique ID, so several
 * boards can be told apart and the PC tool can verify which device it is
 * talking to.
 * -------------------------------------------------------------------------*/

#include "tusb.h"
#include "stm32h7xx.h"
#include "usb_desc_defs.h"

#define USB_LANGID_ENGLISH   0x0409u

/* Endpoint numbers: 0x81 notification (in), 0x02 bulk out, 0x82 bulk in */
#define EPNUM_CDC_NOTIF      0x81u
#define EPNUM_CDC_OUT        0x02u
#define EPNUM_CDC_IN         0x82u
#define CDC_NOTIF_SIZE       8u
#define CDC_BULK_MPS         64u          /* full speed maximum */

enum {
  ITF_NUM_CDC      = 0,   /* communication interface */
  ITF_NUM_CDC_DATA,       /* data interface          */
  ITF_NUM_TOTAL
};

enum { STRID_LANGID = 0, STRID_MANUF, STRID_PRODUCT, STRID_SERIAL };

/* ------------------------------------------------------------------------ */
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100u,

    .iManufacturer      = STRID_MANUF,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,

    .bNumConfigurations = 0x01u,
};

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *) &desc_device;
}

/* ------------------------------------------------------------------------ */
#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[CONFIG_TOTAL_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, CDC_NOTIF_SIZE,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, CDC_BULK_MPS),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_configuration;
}

/* ------------------------------------------------------------------------ */
static const uint16_t desc_string_langid[2] = {
    (TUSB_DESC_STRING << 8) | (2u * 2u + 2u),
    USB_LANGID_ENGLISH,
};

/* The STM32 factory UID is 96 bits at 0x1FF1E800. */
static void uid_to_hex(char *out, uint32_t out_len) {
  const uint32_t *uid = (const uint32_t *) UID_BASE;
  static const char hex[] = "0123456789ABCDEF";
  uint32_t o = 0u;

  for (uint32_t w = 0u; w < 3u; w++) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      if (o + 1u >= out_len) break;
      out[o++] = hex[(uid[w] >> shift) & 0xFu];
    }
  }
  out[o] = '\0';
}

/* Buffer layout: [0] = length|type, then UTF-16 code units. */
#define STRBUF_WORDS   40u
static uint16_t s_strbuf[STRBUF_WORDS];

static uint16_t *strbuf_from_ascii(const char *s) {
  uint32_t len = 0u;
  while (s[len] && (len < (STRBUF_WORDS - 1u))) len++;

  s_strbuf[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2u * len + 2u));
  for (uint32_t i = 0u; i < len; i++) {
    s_strbuf[1u + i] = (uint16_t) s[i];
  }
  return s_strbuf;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  static char serial[25];

  switch (index) {
    case STRID_LANGID:  return desc_string_langid;
    case STRID_MANUF:   return strbuf_from_ascii(USB_MANUF_STR);
    case STRID_PRODUCT: return strbuf_from_ascii(USB_PRODUCT_STR);
    case STRID_SERIAL:  uid_to_hex(serial, sizeof(serial));
                        return strbuf_from_ascii(serial);
    default:            return NULL;
  }
}
