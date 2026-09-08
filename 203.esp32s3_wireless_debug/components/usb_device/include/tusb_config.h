/**
 * @file tusb_config.h
 * @brief TinyUSB device configuration - CMSIS-DAP v1 over HID.
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU               OPT_MCU_ESP32S3
#define CFG_TUSB_OS                OPT_OS_FREERTOS

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN         __attribute__ ((aligned(4)))
#endif

#define CFG_TUD_ENABLED            1
#define CFG_TUD_MAX_SPEED          OPT_MODE_DEFAULT_SPEED

/* Endpoint sizes */
#define CFG_TUD_ENDPOINT0_SIZE     64

/* Device classes: single HID (CMSIS-DAP) */
#define CFG_TUD_HID                1
#define CFG_TUD_CDC                0
#define CFG_TUD_MSC                0
#define CFG_TUD_MIDI               0
#define CFG_TUD_VENDOR             0

#define CFG_TUD_TASK_QUEUE_SZ      16

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
