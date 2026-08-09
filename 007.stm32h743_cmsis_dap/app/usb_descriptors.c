/*----------------------------------------------------------------------------
 * USB descriptors - CMSIS-DAP v1 (HID transport)
 *
 * CMSIS-DAP v1 is "a vendor-defined HID device that moves 64-byte reports".
 * Two properties decide whether a host tool will actually talk to us:
 *
 *  1. The product string must contain "CMSIS-DAP". OpenOCD's HID backend
 *     enumerates every HID device on the bus and picks the ones whose product
 *     string matches that substring (pyOCD does the same), so this is not
 *     cosmetic - it is the discovery mechanism.
 *
 *  2. The interface needs an interrupt OUT endpoint as well as IN. A plain
 *     TUD_HID_DESCRIPTOR only declares IN and would force commands through
 *     control transfers; TUD_HID_INOUT_DESCRIPTOR declares both.
 *
 * Being HID also means Windows binds its in-box driver: no WinUSB/Zadig step,
 * which is the whole reason to prefer v1 over v2 for compatibility.
 *--------------------------------------------------------------------------*/

#include "tusb.h"
#include "bsp.h"

/* Keil/ARM's CMSIS-DAP identity. Widely used by third-party probes and already
 * present in the default VID/PID list of most host tools. */
#define USB_VID   0xC251
#define USB_PID   0xF001
#define USB_BCD   0x0200

/*--------------------------------------------------------------------
 * Device descriptor
 *------------------------------------------------------------------*/
tusb_desc_device_t const desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = USB_BCD,

  /* Class is declared per-interface: the device itself is "see interface". */
  .bDeviceClass       = 0x00,
  .bDeviceSubClass    = 0x00,
  .bDeviceProtocol    = 0x00,

  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,

  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,

  .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

/*--------------------------------------------------------------------
 * HID report descriptor
 *
 * The canonical CMSIS-DAP layout: vendor usage page 0xFF00, one 64-byte
 * input report, one 64-byte output report, no report IDs (report ID 0 means
 * "no prefix byte", so all 64 bytes are payload).
 *------------------------------------------------------------------*/
uint8_t const desc_hid_report[] = {
  0x06, 0x00, 0xFF,   /* Usage Page (Vendor Defined 0xFF00)     */
  0x09, 0x01,         /* Usage (0x01)                            */
  0xA1, 0x01,         /* Collection (Application)                */
  0x15, 0x00,         /*   Logical Minimum (0)                   */
  0x26, 0xFF, 0x00,   /*   Logical Maximum (255)                 */
  0x75, 0x08,         /*   Report Size (8 bits)                  */
  0x95, CFG_TUD_HID_EP_BUFSIZE, /* Report Count (64)             */
  0x09, 0x01,         /*   Usage (0x01)                          */
  0x81, 0x02,         /*   Input (Data, Var, Abs)                */
  0x95, CFG_TUD_HID_EP_BUFSIZE, /* Report Count (64)             */
  0x09, 0x01,         /*   Usage (0x01)                          */
  0x91, 0x02,         /*   Output (Data, Var, Abs)               */
  0x95, 0x01,         /*   Report Count (1)                      */
  0x09, 0x01,         /*   Usage (0x01)                          */
  0xB1, 0x02,         /*   Feature (Data, Var, Abs)              */
  0xC0                /* End Collection                          */
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
}

/*--------------------------------------------------------------------
 * Configuration descriptor
 *------------------------------------------------------------------*/
enum {
  ITF_NUM_HID = 0,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

#define EPNUM_HID_OUT   0x01
#define EPNUM_HID_IN    0x81

uint8_t const desc_configuration[] = {
  /* config number, interface count, string index, total length,
   * attributes, power in mA */
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

  /* interface number, string index, protocol, report descriptor len,
   * EP OUT address, EP IN address, size, polling interval (ms)
   *
   * bInterval = 1 ms: DAP throughput is dominated by how many 64-byte reports
   * per second the host can push, so poll as fast as full speed allows. */
  TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 4, HID_ITF_PROTOCOL_NONE,
                           sizeof(desc_hid_report),
                           EPNUM_HID_OUT, EPNUM_HID_IN,
                           CFG_TUD_HID_EP_BUFSIZE, 1)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

/*--------------------------------------------------------------------
 * String descriptors
 *------------------------------------------------------------------*/
enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_INTERFACE
};

static char serial_str[25];   /* 24 hex chars from the 96-bit UID + NUL */

static char const *string_desc_arr[] = {
  (const char[]){0x09, 0x04},   /* 0: language = English (0x0409) */
  "WorkBuddy",                  /* 1: Manufacturer                */
  "STM32H743 CMSIS-DAP v1",     /* 2: Product - must contain "CMSIS-DAP" */
  serial_str,                   /* 3: Serial, filled from the device UID */
  "CMSIS-DAP v1"                /* 4: HID interface               */
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  uint8_t chr_count;

  if (index == STRID_LANGID) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
      return NULL;
    }

    /* The serial number is derived from the MCU's unique ID the first time it
     * is asked for, so two probes on one host stay distinguishable. */
    if (index == STRID_SERIAL && serial_str[0] == '\0') {
      board_get_unique_id(serial_str, sizeof(serial_str));
    }

    const char *str = string_desc_arr[index];
    chr_count = (uint8_t)strlen(str);
    if (chr_count > 31) {
      chr_count = 31;
    }

    /* ASCII -> UTF-16LE */
    for (uint8_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = str[i];
    }
  }

  /* first byte: length (incl. header), second byte: string type */
  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}
