//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2026-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_dcmi_ov5640.c
//
//  Purpose:
//      dcmi interface.
//
//  Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//	
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#ifndef __DRV_DCMI_OV5640_H
#define __DRV_DCMI_OV5640_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define DCMI_OV5640_ID              0x5640

#define DCMI_CHIPID_H          	    0x300A  	// 芯片ID寄存器 高字节
#define DCMI_CHIPID_L               0x300B

#define OV5640_GroupAccess			0X3212	// 寄存器组访问
#define OV5640_TIMING_DVPHO_H		0x3808	// 输出水平尺寸,高字节
#define OV5640_TIMING_DVPHO_L		0x3809	// 输出水平尺寸,低字节
#define OV5640_TIMING_DVPVO_H		0x380A	// 输出垂直尺寸,高字节
#define OV5640_TIMING_DVPVO_L		0x380B   // 输出垂直尺寸,低字节
#define OV5640_TIMING_Flip			0x3820	// Bit[2:1]用于设置是否垂直翻转
#define OV5640_TIMING_Mirror		0x3821	// Bit[2:1]用于设置是否水平镜像

#define OV5640_AF_CMD_MAIN			0x3022	// AF 主命令
#define OV5640_AF_CMD_ACK			0x3023	// AF 命令确认
#define OV5640_AF_FW_STATUS			0x3029	// 对焦状态寄存器

// 1. 定义OV5640实际输出的图像大小，可以根据实际的应用或者显示屏进行调整
// 2. 这两个参数不会影响帧率
// 3. 因为配置的OV5640的ISP窗口比例为4:3(1280*960)，用户设置的输出尺寸也应满足这个比例
// 4. 如果需要其它比例，需要修改初始化配置里的参数
// 本工程使用 QVGA(320x240) 作为传感器输出：4:3 比例，OV5640 缩放器的常规档位，
// 再由 DCMI 裁剪出居中的 192x192 送给显示和神经网络。
#define OV5640_WIDTH                320   // 图像长度
#define OV5640_HEIGHT               240   // 图像宽度

GlobalType_t drv_dcmi_ov5640_init(void);

#ifdef __cplusplus
}
#endif

#endif