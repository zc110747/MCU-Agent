//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2026-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_dcmi.c
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
#include "drv_dcmi.h"
#include "drv_dcmi_ov5640.h"

extern I2C_HandleTypeDef hi2c4;
extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef hdma_dcmi;

volatile uint8_t g_dcmi_framestate = 0;  // DCMI状态标志，当数据帧传输完成时，会被 HAL_DCMI_FrameEventCallback() 中断回调函数置 1     
volatile uint8_t g_dcmi_fps ;            // 帧率

static void dcmi_reset(void);
static uint16_t dcmi_read_id(void);

GlobalType_t drv_dcmi_init(void)
{ 
    uint16_t chip_id;
    
    dcmi_reset();
    
    chip_id = dcmi_read_id();
    
    if (chip_id == 0x5640)
    {
        if (drv_dcmi_ov5640_init() != RT_OK)
        {
            return RT_FAIL;
        }
    }
    else
    {
        return RT_FAIL;
    }
    

    return RT_OK;
}

void drv_dcmi_dma_continuous(uint32_t DMA_Buffer, uint32_t DMA_BufferSize)
{
   hdma_dcmi.Init.Mode  = DMA_CIRCULAR;  // 循环模式					

   HAL_DMA_Init(&hdma_dcmi);    // 配置DMA

  // 使能DCMI采集数据,连续采集模式
   HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS, (uint32_t)DMA_Buffer,DMA_BufferSize);
}

static void dcmi_reset(void)
{
    HAL_Delay(30);
    
    DCMI_PWDN_OFF();
    
    // wait for hardware reset, and start sccb config
    HAL_Delay(30);
    
    // set the clock and soft reset
	sccb_write_reg(0x3103, 0x11);
	sccb_write_reg(0x3008, 0x82);
    
	HAL_Delay(5);
    
}

static uint16_t dcmi_read_id(void)
{
   uint8_t id_h = 0, id_l = 0;
   GlobalType_t res;
    
   res = sccb_read_reg(DCMI_CHIPID_H, &id_h);
    
   res |= sccb_read_reg(DCMI_CHIPID_L, &id_l);
	
   return ((uint16_t)id_h<<8)|id_l;
}

GlobalType_t sccb_write_buffer(uint16_t addr, uint8_t *pdata, uint16_t size)
{
    HAL_StatusTypeDef res;
    
    res = HAL_I2C_Mem_Write(&hi2c4, DCMI_DEVICE_ADDRESS, addr, I2C_MEMADD_SIZE_16BIT, pdata, size, 1000);   
    if(res != HAL_OK)
    {
        return RT_FAIL;
    }
    
    return RT_OK;     
}

GlobalType_t sccb_write_reg(uint16_t addr, uint8_t data)
{
    HAL_StatusTypeDef res;
    
    res = HAL_I2C_Mem_Write(&hi2c4, DCMI_DEVICE_ADDRESS, addr, I2C_MEMADD_SIZE_16BIT, &data, 1, DCMI_TIMEOUT);   
    if(res != HAL_OK)
    {
        return RT_FAIL;
    }
    
    return RT_OK; 
}

GlobalType_t sccb_read_reg(uint16_t addr, uint8_t *rdata)
{
    uint8_t res;
    
    res = HAL_I2C_Mem_Read(&hi2c4, DCMI_DEVICE_ADDRESS, addr, I2C_MEMADD_SIZE_16BIT, rdata, 1, DCMI_TIMEOUT);  
    
    if(res != HAL_OK)
    {
        return RT_FAIL;
    }
    
    return RT_OK;
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
    static uint32_t DCMI_Tick = 0; 
    static uint8_t  DCMI_Frame_Count = 0;

 	if(HAL_GetTick() - DCMI_Tick >= 1000)
	{
		DCMI_Tick = HAL_GetTick();
		
		g_dcmi_fps = DCMI_Frame_Count;

		DCMI_Frame_Count = 0; 
	}
	DCMI_Frame_Count ++; 

    g_dcmi_framestate = 1;
}
