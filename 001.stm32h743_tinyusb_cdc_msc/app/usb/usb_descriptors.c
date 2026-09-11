/* ---------------------------------------------------------------------------
 * USB descriptors - composite CDC-ACM (virtual COM port) + Mass Storage (U-disk)
 *
 * The device advertises itself as Miscellaneous / Common / IAD at the device
 * level. That is what makes Windows bind its in-box usbser.sys driver to the
 * CDC function automatically, with no .inf file. The extra Mass Storage
 * interface is claimed by Windows' generic usbstor driver and shown as a
 * removable disk. Linux and macOS pick both up as /dev/ttyACM* (or cu.usbmodem*)
 * and a mounted mass-storage volume.
 * -------------------------------------------------------------------------*/

#include "tusb.h"
#include "stm32h7xx.h"

/* TinyUSB's example VID. 0xCafe is not registered to anyone - fine for
 * development, but do get your own VID/PID before shipping a product. */
#define USB_VID   0xCafe
#define USB_BCD   0x0200   /* USB 2.0 */

/* PID is derived from the enabled class set, the same scheme TinyUSB's own
 * examples use, so several demo devices can coexist on one host. */
#define _PID_MAP(itf, n) ((CFG_TUD_##itf) << (n))
#define USB_PID   (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | \
                            _PID_MAP(HID, 2) | _PID_MAP(MIDI, 3) | _PID_MAP(VENDOR, 4))

/* ------------------------------------------------------------------------ */
/* Device descriptor                                                         */
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
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01,
};

const uint8_t* tud_descriptor_device_cb(void) {
  return (const uint8_t*) &desc_device;
}

/* ------------------------------------------------------------------------ */
/* Configuration descriptor                                                  */
/* ------------------------------------------------------------------------ */
enum {
  ITF_NUM_CDC = 0,   /* communication interface */
  ITF_NUM_CDC_DATA,  /* data interface          */
  ITF_NUM_MSC,       /* mass storage interface  */
  ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_MSC_OUT     0x03
#define EPNUM_MSC_IN      0x83

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static const uint8_t desc_fs_configuration[] = {
    /* config number, interface count, string index, total length,
       attributes, power in mA */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    /* interface number, string index, EP notification address and size,
       EP data address (out, in) and size */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    /* interface number, string index, EP OUT, EP IN, EP packet size */
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_fs_configuration;
}

/* ------------------------------------------------------------------------ */
/* String descriptors                                                        */
/* ------------------------------------------------------------------------ */
enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL, STRID_CDC, STRID_MSC };

static const char* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },   /* 0: English (0x0409), as raw bytes */
    "STM32",                         /* 1: Manufacturer */
    "H743 TinyUSB CDC",              /* 2: Product      */
    NULL,                            /* 3: Serial - generated from the chip UID */
    "H743 CDC Interface",            /* 4: CDC interface */
    "H743 MSC Interface",            /* 5: MSC interface */
};

/* Two bytes of header + up to 32 UTF-16 code units */
static uint16_t _desc_str[33];

/* Turn the 96-bit unique device ID into a 24-char hex serial number, so every
 * board enumerates as a distinct device instead of sharing one COM port. */
static uint8_t serial_from_uid(uint16_t* utf16, uint8_t max_chars) {
  static const char hex[] = "0123456789ABCDEF";
  const uint32_t* uid = (const uint32_t*) UID_BASE;
  uint8_t n = 0;

  for (uint8_t w = 0; w < 3 && n + 8 <= max_chars; w++) {
    uint32_t v = uid[w];
    for (int8_t nib = 7; nib >= 0; nib--) {
      utf16[n++] = (uint16_t) hex[(v >> (nib * 4)) & 0x0F];
    }
  }
  return n;
}

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  uint8_t chr_count = 0;

  switch (index) {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[STRID_LANGID], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL:
      chr_count = serial_from_uid(&_desc_str[1], 32);
      break;

    default:
      if (index >= TU_ARRAY_SIZE(string_desc_arr)) return NULL;
      if (string_desc_arr[index] == NULL)          return NULL;
      {
        const char* str = string_desc_arr[index];
        chr_count = (uint8_t) strlen(str);
        if (chr_count > 32) chr_count = 32;
        for (uint8_t i = 0; i < chr_count; i++) {
          _desc_str[1 + i] = str[i];       /* ASCII -> UTF-16LE */
        }
      }
      break;
  }

  /* First word: length in bytes (header included) and descriptor type */
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
