/**
 ******************************************************************************
 * @file    usb_descriptors.c
 * @brief   UVC 1.5 descriptors: one camera terminal streaming YUY2 240x240.
 ******************************************************************************
 */

#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

#define USB_VID   0xCAFE
#define USB_PID   0x4020
#define USB_BCD   0x0200

/* Deprecated UVC time-stamp base clock. */
#define UVC_CLOCK_FREQUENCY   27000000

#define UVC_ENTITY_CAP_INPUT_TERMINAL   0x01
#define UVC_ENTITY_CAP_OUTPUT_TERMINAL  0x02

enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_UVC_CONTROL,
  STRID_UVC_STREAMING,
};

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, /* 0: English (0x0409) */
    "STM32",                    /* 1: Manufacturer     */
    "STM32H743 OV5640 UVC",     /* 2: Product          */
    NULL,                       /* 3: Serial, from MCU unique ID */
    "UVC Control",              /* 4 */
    "UVC Streaming",            /* 5 */
};

/* ==========================================================================
 * Device descriptor
 * ========================================================================== */
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    /* UVC needs an Interface Association Descriptor */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0102,   /* bump -> forces host to drop cached descriptor */

    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,

    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void)
{
  return (uint8_t const *)&desc_device;
}

/* ==========================================================================
 * Configuration descriptor
 * ========================================================================== */
typedef struct TU_ATTR_PACKED {
  tusb_desc_interface_t                     itf;
  tusb_desc_video_control_header_1itf_t     header;
  tusb_desc_video_control_camera_terminal_t camera_terminal;
  tusb_desc_video_control_output_terminal_t output_terminal;
} uvc_control_desc_t;

typedef struct TU_ATTR_PACKED {
  tusb_desc_interface_t                            itf;
  tusb_desc_video_streaming_input_header_1byte_t   header;
  tusb_desc_video_format_uncompressed_t            format;
  /* One *discrete* interval, deliberately. With a continuous range the Windows
   * UVC driver is free to pick the slowest entry, and it does: advertising
   * 8..1 fps got us a 1 fps stream. A single discrete value removes the
   * ambiguity and pins the host to the rate the USB FS budget can actually
   * sustain. */
  tusb_desc_video_frame_uncompressed_1int_t        frame;
  tusb_desc_video_streaming_color_matching_t       color;
  tusb_desc_interface_t                            itf_alt; /* alt 1 carries the ISO EP */
  tusb_desc_endpoint_t                             ep;
} uvc_streaming_desc_t;

typedef struct TU_ATTR_PACKED {
  tusb_desc_configuration_t   config;
  tusb_desc_interface_assoc_t iad;
  uvc_control_desc_t          video_control;
  uvc_streaming_desc_t        video_streaming;
} uvc_cfg_desc_t;

static const uvc_cfg_desc_t desc_fs_configuration = {
    .config = {
        .bLength             = sizeof(tusb_desc_configuration_t),
        .bDescriptorType     = TUSB_DESC_CONFIGURATION,
        .wTotalLength        = sizeof(uvc_cfg_desc_t),
        .bNumInterfaces      = ITF_NUM_TOTAL,
        .bConfigurationValue = 1,
        .iConfiguration      = 0,
        .bmAttributes        = TU_BIT(7),   /* bus powered */
        .bMaxPower           = 250 / 2,     /* 250 mA */
    },

    .iad = {
        .bLength           = sizeof(tusb_desc_interface_assoc_t),
        .bDescriptorType   = TUSB_DESC_INTERFACE_ASSOCIATION,
        .bFirstInterface   = ITF_NUM_VIDEO_CONTROL,
        .bInterfaceCount   = 2,
        .bFunctionClass    = TUSB_CLASS_VIDEO,
        .bFunctionSubClass = VIDEO_SUBCLASS_INTERFACE_COLLECTION,
        .bFunctionProtocol = VIDEO_ITF_PROTOCOL_UNDEFINED,
        .iFunction         = 0,
    },

    /* ---------------- Video Control interface ---------------- */
    .video_control = {
        .itf = {
            .bLength            = sizeof(tusb_desc_interface_t),
            .bDescriptorType    = TUSB_DESC_INTERFACE,
            .bInterfaceNumber   = ITF_NUM_VIDEO_CONTROL,
            .bAlternateSetting  = 0,
            .bNumEndpoints      = 0,
            .bInterfaceClass    = TUSB_CLASS_VIDEO,
            .bInterfaceSubClass = VIDEO_SUBCLASS_CONTROL,
            .bInterfaceProtocol = VIDEO_ITF_PROTOCOL_15,
            .iInterface         = STRID_UVC_CONTROL,
        },
        .header = {
            .bLength            = sizeof(tusb_desc_video_control_header_1itf_t),
            .bDescriptorType    = TUSB_DESC_CS_INTERFACE,
            .bDescriptorSubType = VIDEO_CS_ITF_VC_HEADER,
            .bcdUVC             = VIDEO_BCD_1_50,
            .wTotalLength       = sizeof(uvc_control_desc_t) - sizeof(tusb_desc_interface_t),
            .dwClockFrequency   = UVC_CLOCK_FREQUENCY,
            .bInCollection      = 1,
            .baInterfaceNr      = {ITF_NUM_VIDEO_STREAMING},
        },
        .camera_terminal = {
            .bLength                  = sizeof(tusb_desc_video_control_camera_terminal_t),
            .bDescriptorType          = TUSB_DESC_CS_INTERFACE,
            .bDescriptorSubType       = VIDEO_CS_ITF_VC_INPUT_TERMINAL,
            .bTerminalID              = UVC_ENTITY_CAP_INPUT_TERMINAL,
            .wTerminalType            = VIDEO_ITT_CAMERA,
            .bAssocTerminal           = 0,
            .iTerminal                = 0,
            .wObjectiveFocalLengthMin = 0,
            .wObjectiveFocalLengthMax = 0,
            .wOcularFocalLength       = 0,
            .bControlSize             = 3,
            .bmControls               = {0, 0, 0},
        },
        .output_terminal = {
            .bLength            = sizeof(tusb_desc_video_control_output_terminal_t),
            .bDescriptorType    = TUSB_DESC_CS_INTERFACE,
            .bDescriptorSubType = VIDEO_CS_ITF_VC_OUTPUT_TERMINAL,
            .bTerminalID        = UVC_ENTITY_CAP_OUTPUT_TERMINAL,
            .wTerminalType      = VIDEO_TT_STREAMING,
            .bAssocTerminal     = 0,
            .bSourceID          = UVC_ENTITY_CAP_INPUT_TERMINAL,
            .iTerminal          = 0,
        },
    },

    /* ---------------- Video Streaming interface ---------------- */
    .video_streaming = {
        .itf = {
            .bLength            = sizeof(tusb_desc_interface_t),
            .bDescriptorType    = TUSB_DESC_INTERFACE,
            .bInterfaceNumber   = ITF_NUM_VIDEO_STREAMING,
            .bAlternateSetting  = 0,
            .bNumEndpoints      = 0,      /* alt 0 has no bandwidth */
            .bInterfaceClass    = TUSB_CLASS_VIDEO,
            .bInterfaceSubClass = VIDEO_SUBCLASS_STREAMING,
            .bInterfaceProtocol = VIDEO_ITF_PROTOCOL_15,
            .iInterface         = STRID_UVC_STREAMING,
        },
        .header = {
            .bLength            = sizeof(tusb_desc_video_streaming_input_header_1byte_t),
            .bDescriptorType    = TUSB_DESC_CS_INTERFACE,
            .bDescriptorSubType = VIDEO_CS_ITF_VS_INPUT_HEADER,
            .bNumFormats        = 1,
            .wTotalLength       = sizeof(uvc_streaming_desc_t)
                                  - sizeof(tusb_desc_interface_t)   /* alt 0 */
                                  - sizeof(tusb_desc_interface_t)   /* alt 1 */
                                  - sizeof(tusb_desc_endpoint_t),
            .bEndpointAddress   = EPNUM_VIDEO_IN,
            .bmInfo             = 0,
            .bTerminalLink      = UVC_ENTITY_CAP_OUTPUT_TERMINAL,
            .bStillCaptureMethod= 0,
            .bTriggerSupport    = 0,
            .bTriggerUsage      = 0,
            .bControlSize       = 1,
            .bmaControls        = {0},
        },
        .format = {
            .bLength              = sizeof(tusb_desc_video_format_uncompressed_t),
            .bDescriptorType      = TUSB_DESC_CS_INTERFACE,
            .bDescriptorSubType   = VIDEO_CS_ITF_VS_FORMAT_UNCOMPRESSED,
            .bFormatIndex         = 1,
            .bNumFrameDescriptors = 1,
            .guidFormat           = {TUD_VIDEO_GUID_YUY2},
            .bBitsPerPixel        = 16,
            .bDefaultFrameIndex   = 1,
            .bAspectRatioX        = 0,
            .bAspectRatioY        = 0,
            .bmInterlaceFlags     = 0,
            .bCopyProtect         = 0,
        },
        .frame = {
            .bLength                   = sizeof(tusb_desc_video_frame_uncompressed_1int_t),
            .bDescriptorType           = TUSB_DESC_CS_INTERFACE,
            .bDescriptorSubType        = VIDEO_CS_ITF_VS_FRAME_UNCOMPRESSED,
            .bFrameIndex               = 1,
            .bmCapabilities            = 0,
            .wWidth                    = FRAME_WIDTH,
            .wHeight                   = FRAME_HEIGHT,
            .dwMinBitRate              = FRAME_WIDTH * FRAME_HEIGHT * 16 * FRAME_RATE,
            .dwMaxBitRate              = FRAME_WIDTH * FRAME_HEIGHT * 16 * FRAME_RATE,
            .dwMaxVideoFrameBufferSize = FRAME_SIZE,
            .dwDefaultFrameInterval    = 10000000 / FRAME_RATE,
            .bFrameIntervalType        = 1, /* one discrete interval */
            .dwFrameInterval           = {10000000 / FRAME_RATE},
        },
        .color = {
            .bLength                  = sizeof(tusb_desc_video_streaming_color_matching_t),
            .bDescriptorType          = TUSB_DESC_CS_INTERFACE,
            .bDescriptorSubType       = VIDEO_CS_ITF_VS_COLORFORMAT,
            .bColorPrimaries          = VIDEO_COLOR_PRIMARIES_BT709,
            .bTransferCharacteristics = VIDEO_COLOR_XFER_CH_BT709,
            .bMatrixCoefficients      = VIDEO_COLOR_COEF_SMPTE170M,
        },
        .itf_alt = {
            .bLength            = sizeof(tusb_desc_interface_t),
            .bDescriptorType    = TUSB_DESC_INTERFACE,
            .bInterfaceNumber   = ITF_NUM_VIDEO_STREAMING,
            .bAlternateSetting  = 1,
            .bNumEndpoints      = 1,
            .bInterfaceClass    = TUSB_CLASS_VIDEO,
            .bInterfaceSubClass = VIDEO_SUBCLASS_STREAMING,
            .bInterfaceProtocol = VIDEO_ITF_PROTOCOL_15,
            .iInterface         = STRID_UVC_STREAMING,
        },
        .ep = {
            .bLength          = sizeof(tusb_desc_endpoint_t),
            .bDescriptorType  = TUSB_DESC_ENDPOINT,
            .bEndpointAddress = EPNUM_VIDEO_IN,
            .bmAttributes     = {
                .xfer = TUSB_XFER_ISOCHRONOUS,
                .sync = 1, /* asynchronous */
            },
            .wMaxPacketSize = CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE,
            .bInterval      = 1,
        },
    },
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
  (void)index;
  return (uint8_t const *)&desc_fs_configuration;
}

/* ==========================================================================
 * String descriptors
 * ========================================================================== */
static uint16_t _desc_str[32 + 1];

/* Build a hex serial number out of the 96-bit MCU unique ID. */
static size_t board_serial(uint16_t *utf16, size_t max_chars)
{
  static const char hex[] = "0123456789ABCDEF";
  const uint32_t *uid = (const uint32_t *)UID_BASE;
  size_t n = 0;

  for (int w = 0; w < 3 && n + 8 <= max_chars; w++) {
    uint32_t v = uid[w];
    for (int i = 7; i >= 0; i--) {
      utf16[n + (size_t)i] = (uint16_t)hex[v & 0xFU];
      v >>= 4;
    }
    n += 8;
  }
  return n;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void)langid;
  size_t chr_count;

  switch (index) {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL:
      chr_count = board_serial(_desc_str + 1, 32);
      break;

    default:
      if (index >= TU_ARRAY_SIZE(string_desc_arr)) {
        return NULL;
      }
      {
        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        const size_t max_count = (sizeof(_desc_str) / sizeof(_desc_str[0])) - 1;
        if (chr_count > max_count) {
          chr_count = max_count;
        }
        for (size_t i = 0; i < chr_count; i++) {
          _desc_str[1 + i] = (uint16_t)str[i];
        }
      }
      break;
  }

  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
