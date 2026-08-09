//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2023-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_flash.c
//
//  Purpose:
//      driver for flash module.
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
#include "drv_flash.h"

// Flash操作相关宏定义
#define FLASH_START_ADDR          0x08000000     // Flash起始地址

static uint32_t flash_get_sector(uint32_t address)
{
    uint32_t sector = 0;
    uint32_t offset = address - FLASH_START_ADDR;
    
    if (address < 0x08100000) { // Bank1
        sector = offset / FLASH_SECTOR_SIZE;
        if (sector > 7) sector = 7; // Bank1有8个sector
    } else { // Bank2
        offset = address - 0x08100000;
        sector = offset / FLASH_SECTOR_SIZE + 8;
        if (sector > 15) sector = 15; // Bank2有8个sector
    }
    
    return sector;
}

static uint32_t flash_get_banks(uint32_t address)
{
    if (address < 0x08100000) { // Bank1
        return FLASH_BANK_1;
    } else {
        return FLASH_BANK_2;
    }
}    

GlobalType_t flash_update_empty_device(uint32_t address, uint32_t sector_num)
{
    GlobalType_t ret = RT_OK;
    uint32_t sector_error = 0;
    
    if (sector_num == 0) {
        return RT_OK;
    }
    
    HAL_FLASH_Unlock();
    
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1);
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);
    
    FLASH_EraseInitTypeDef erase_init = {
        .TypeErase = FLASH_TYPEERASE_SECTORS,
        .Banks = flash_get_banks(address),
        .Sector = flash_get_sector(address),
        .NbSectors = sector_num,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3
    };

    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    
    if (status != HAL_OK) {
        ret = RT_FAIL;
    }
    
    HAL_FLASH_Lock();
    
    return ret;
}

GlobalType_t flash_update_write_device(uint32_t address, uint32_t *pbuffer, uint32_t size)
{
    GlobalType_t ret = RT_OK;
    
    if (size == 0 || pbuffer == NULL) {
        return RT_OK;
    }

    __disable_irq();
    HAL_FLASH_Unlock();
    
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1);
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);
    
    size = size/32 + (size%32?1:0);
    for (uint32_t i = 0; i < size; i++) {
        if (address < FLASH_START_ADDR || address >= (FLASH_START_ADDR + 2 * 1024 * 1024)) {
            ret = RT_FAIL;
            break;
        }

        HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, 
                                                      address, 
                                                      (uint32_t)pbuffer);
        
        if (status != HAL_OK) {
            ret = RT_FAIL;
            break;
        }
        
        address += 32;        // 每次增加32字节
        pbuffer += 8;         // 移动32字节
    }
    
    HAL_FLASH_Lock();
    __enable_irq();
    
    return ret;
}
