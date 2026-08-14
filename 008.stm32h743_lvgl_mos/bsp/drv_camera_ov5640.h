/**
  * @file    drv_camera_ov5640.h
  * @brief   OV5640 sensor register table / init sequence (DCMI + I2C4/SCCB).
  *
  * The register dump and the auto-focus firmware blob are the OV5640
  * application-note defaults and must be copied verbatim; only the public
  * init function name and the logging macro differ from the #005 port.
  */
#ifndef __DRV_CAMERA_OV5640_H
#define __DRV_CAMERA_OV5640_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define DCMI_OV5640_ID              0x5640

#define DCMI_CHIPID_H          	    0x300A  	/* chip ID register, high byte */
#define DCMI_CHIPID_L               0x300B

#define OV5640_GroupAccess			0X3212	/* register group access        */
#define OV5640_TIMING_DVPHO_H		0x3808	/* output horizontal size, high */
#define OV5640_TIMING_DVPHO_L		0x3809	/* output horizontal size, low  */
#define OV5640_TIMING_DVPVO_H		0x380A	/* output vertical size, high   */
#define OV5640_TIMING_DVPVO_L		0x380B   /* output vertical size, low    */
#define OV5640_TIMING_Flip			0x3820	/* Bit[2:1] vertical flip        */
#define OV5640_TIMING_Mirror		0x3821	/* Bit[2:1] horizontal mirror   */

#define OV5640_AF_CMD_MAIN			0x3022	/* AF main command              */
#define OV5640_AF_CMD_ACK			0x3023	/* AF command ack               */
#define OV5640_AF_FW_STATUS			0x3029	/* AF status register           */

/* OV5640 native output is QVGA 320x240 (4:3, ISP scaler).  The DCMI crop
 * in drv_camera.c then centered-crops this down to 96x96 for display.      */
#define OV5640_WIDTH                320   /* pixels */
#define OV5640_HEIGHT               240   /* pixels */

/** Probe + configure the sensor (reset, ID check, register table, frame size,
 *  constant-focus trigger, AF firmware download).  Returns RT_OK / RT_FAIL. */
GlobalType_t drv_camera_ov5640_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_CAMERA_OV5640_H */
