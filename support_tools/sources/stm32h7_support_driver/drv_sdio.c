//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2023-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_sdio.c
//
//  Purpose:
//      driver for sdio module.
//
//  Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//	
//
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#include "drv_sdio.h"
#include "drv_target.h"

extern SD_HandleTypeDef hsd1;

GlobalType_t drv_sdcard_init(void)
{   
    return RT_OK;
}

HAL_StatusTypeDef sdcard_read_disk(uint8_t *buf, uint32_t startBlocks, uint32_t NumberOfBlocks)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint16_t tick = 0;
    
    status = HAL_SD_ReadBlocks(&hsd1, (uint8_t*)buf, startBlocks, NumberOfBlocks, SDMMC_READ_WRITE_TIMEOUT);
    if (status != HAL_OK) {
        return status;
    }
    
    //wait card ok.
    while((HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
    && (tick < SDMMC_READ_WRITE_TIMEOUT))
    {
        hal_delay_ms(1);
        tick++;
    }
    if(tick >= SDMMC_READ_WRITE_TIMEOUT)
    {
        return HAL_TIMEOUT;
    }
    return status;
}

HAL_StatusTypeDef sdcard_write_disk(const uint8_t *buf, uint32_t startBlocks, uint32_t NumberOfBlocks)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint16_t tick = 0;

    status = HAL_SD_WriteBlocks(&hsd1, (uint8_t*)buf, startBlocks, NumberOfBlocks, SDMMC_READ_WRITE_TIMEOUT);
    if (status != HAL_OK) {
        return status;
    }
    
    //wait card ok.
    while((HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
    && (tick < SDMMC_READ_WRITE_TIMEOUT))
    {
        hal_delay_ms(1);
        tick++;
    }
    if(tick >= SDMMC_READ_WRITE_TIMEOUT)
    {
        return HAL_TIMEOUT;
    }

    return status;
}
