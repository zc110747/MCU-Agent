/*--------------------------------------------------------------------
 * TinyUSB configuration for STM32F429IGT6 USB OTG FS Host (MSC).
 * CFG_TUSB_MCU / CFG_TUSB_OS are supplied on the command line by CMake
 * (-DCFG_TUSB_MCU=OPT_MCU_STM32F4 -DCFG_TUSB_OS=OPT_OS_FREERTOS).
 *--------------------------------------------------------------------*/
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_FREERTOS
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

/* Memory section/align macros (left default; RAM is not a constraint here). */
#ifndef CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_SECTION
#endif
#ifndef CFG_TUH_MEM_ALIGN
#define CFG_TUH_MEM_ALIGN     __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// Host Configuration
//--------------------------------------------------------------------
#define CFG_TUH_ENABLED       1

// RHPort used for host (OTG FS on F429 = port 0).
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      0
#endif

// FS-only controller: cap the speed to full speed.
#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED   OPT_MODE_FULL_SPEED
#endif
#define CFG_TUH_MAX_SPEED     BOARD_TUH_MAX_SPEED

//--------------------------------------------------------------------
// Driver Configuration
//--------------------------------------------------------------------
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUH_HUB                 1
#define CFG_TUH_MSC                 1
#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 0
#define CFG_TUH_VENDOR              0

// max device support (excluding hub device)
#define CFG_TUH_DEVICE_MAX          (3 * CFG_TUH_HUB + 1)

//------------- MSC -------------//
#define CFG_TUH_MSC_MAXLUN    4

#ifdef __cplusplus
 }
#endif

#endif /* TUSB_CONFIG_H_ */
