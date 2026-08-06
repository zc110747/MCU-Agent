//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2023-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_oled_text.c
//
//  Purpose:
//      lcd driver write interface.
//
// Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#include "drv_oled_text.h"
#include "ff.h"

#define FONT_NUMS   5

// global parameter

// local parameter
typedef struct
{ 
    FIL fil_list[FONT_NUMS];
    
    uint8_t fil_valid[FONT_NUMS];
}LCD_FS_INFO;
static LCD_FS_INFO g_lcd_fs_info;

static const char* font_name[FONT_NUMS] = {
    "0:/SYSTEM/FONT/UNIGBK.BIN",
    "0:/SYSTEM/FONT/GBK12.FON",
    "0:/SYSTEM/FONT/GBK16.FON",
    "0:/SYSTEM/FONT/GBK24.FON",
    "0:/SYSTEM/FONT/GBK32.FON",
};

// local function

GlobalType_t lcd_driver_font_init(void)
{
    FRESULT res;
    uint8_t index;
    
    memset(&g_lcd_fs_info, 0, sizeof(g_lcd_fs_info));
    
    // 打开文件, 用于后续读取
    for (index=0; index<FONT_NUMS; index++)
    {
        res = f_open(&g_lcd_fs_info.fil_list[index], font_name[index], FA_READ);
        if (res != FR_OK) {
            continue;
        } else {
            g_lcd_fs_info.fil_valid[index] = 1;
        }
    }        
    
    return RT_OK;
}

/**
 * @brief 将字模数据从 MSB+列扫描 转换为 LSB+行扫描
 * @param src  源数据 (MSB, 按列扫描)
 * @param dst  目标数据 (LSB, 按行扫描)
 * @param width  字体宽度 (如 24)
 * @param height 字体高度 (如 24)
 */
void Convert_Font_MSB_Column_to_LSB_Row(uint8_t *src, uint8_t *dst, uint16_t width, uint16_t height)
{
    uint16_t bytes_per_col = (height + 7) / 8;
    uint16_t bytes_per_row = (width + 7) / 8;
    
    memset(dst, 0, bytes_per_row * height);
    
    for(uint16_t col = 0; col < width; col++)
    {
        for(uint16_t byte_idx = 0; byte_idx < bytes_per_col; byte_idx++)
        {
            uint8_t src_byte = src[col * bytes_per_col + byte_idx];
            
            for(uint8_t bit = 0; bit < 8; bit++)
            {
                uint16_t row = byte_idx * 8 + bit;
                if(row >= height) break;

                uint8_t bit_val = (src_byte & (0x80 >> bit)) ? 1 : 0;
                
                uint16_t dst_byte_idx = row * bytes_per_row + col / 8;
                uint8_t dst_bit_pos = col % 8;
                
                if(bit_val)
                    dst[dst_byte_idx] |= (1 << dst_bit_pos);
                else
                    dst[dst_byte_idx] &= ~(1 << dst_bit_pos);
            }
        }
    }
}

static uint8_t read_buffer[192] = {0};
//code 字符指针开始
//从字库中查找出字模
//code 字符串的开始地址,GBK码
//mat  数据存放地址 (size/8+((size%8)?1:0))*(size) bytes大小	
//size:字体大小
GlobalType_t lcd_driver_get_hzmat(uint8_t *code, uint8_t *pbuffer, pFONT *font)
{		    
	uint8_t qh, ql;				  
	uint32_t foffset; 
    UINT bytes_read;
    FRESULT res;
    uint8_t file_index = 0;
    
	qh=*code;
	ql=*(++code);
    
	if (qh < 0x81 || ql < 0x40 
    || ql == 0xff || qh == 0xff) //非常用汉字
	{   		    
        goto __fail;
	}
    
	if(ql < 0x7f) ql -= 0x40;
	else ql -= 0x41;
	qh -= 0x81;
	foffset = ((uint32_t)190*qh + ql) * font->Sizes;	//得到字库中的字节偏移量 
    
	switch(font->Height)
	{
		case 12:
            file_index = 1;
			break;
		case 16:
            file_index = 2;
			break;
		case 24:
            file_index = 3;
			break;
		case 32:
            file_index = 4;
			break;		
	} 

    if (!g_lcd_fs_info.fil_valid[file_index])
        goto __fail;
    res = f_lseek(&g_lcd_fs_info.fil_list[file_index], foffset);
    if (res != FR_OK)
        goto __fail; 
    res = f_read(&g_lcd_fs_info.fil_list[file_index], read_buffer, font->Sizes, &bytes_read);
    if (res != FR_OK || bytes_read != font->Sizes)
        goto __fail; 
    
    Convert_Font_MSB_Column_to_LSB_Row(read_buffer, pbuffer, font->Width, font->Height);
    
    return RT_OK;
    
__fail:
    memset(pbuffer, 0, font->Sizes);
    
    return RT_FAIL;
} 

pFONT CH_TEXT_Font12 = { 
    NULL, 
    12, 
    12, 
    24,	
    0, 
};

pFONT CH_TEXT_Font16 = { 
    NULL, 
    16, 
    16, 
    32,	
    0, 
};

pFONT CH_TEXT_Font24 = { 
    NULL, 
    24, 
    24, 
    72,	
    0, 
};

pFONT CH_TEXT_Font32 = { 
    NULL, 
    32, 
    32, 
    128,	
    0, 
};
