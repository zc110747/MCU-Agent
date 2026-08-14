/**
  ******************************************************************************
  * @file    drv_spi_oled.c
  * @brief   240x240 ST7789 panel driver over SPI6.
  *
  *  The panel is wired half duplex (MOSI only), so every access is a write.
  *  Pixel pushes temporarily switch SPI6 to 16 bit frames to halve the number
  *  of FIFO accesses, then switch back to 8 bit for command/parameter bytes.
  ******************************************************************************
  */
#include "drv_spi_oled.h"
#include "drv_oled_text.h"

#define LCD_SPI hspi6

static OLED_INFO g_oled_info = {0};
static pFONT   *LCD_AsciiFonts;     /* ASCII font currently selected    */
static pFONT   *LCD_CHFonts;        /* Chinese font currently selected  */
static uint16_t LCD_Buff[1024];     /* one glyph worth of RGB565 pixels */

/* ---------------------------------------------------------------------------
 * Shadow framebuffer
 * ---------------------------------------------------------------------------
 * The ST7789 is driven write-only (MOSI only, no read-back), so to "capture
 * the current page" we keep a software mirror of the controller GRAM in
 * RAM_D1.  Every pixel-pushing path (LCD_Clear / LCD_Fill / LCD_CopyBuffer
 * / the two glyph renderers) writes both the panel and this buffer, so the
 * buffer always holds exactly what is on screen.  Screenshot code reads it
 * directly and feeds it to the JPEG encoder.
 *
 * The buffer is indexed by controller GRAM coordinates (0..239 each), which
 * for the default portrait orientation (X/Y offset 0) are identical to the
 * logical coordinates used by the draw calls.
 * ------------------------------------------------------------------------- */
static uint16_t g_lcd_fb[LCD_Width * LCD_Height];   /* 240x240 RGB565 = 112.5 KB */

static void fb_fill_rect(int gx, int gy, int gw, int gh, uint16_t color)
{
    if (gw <= 0 || gh <= 0) return;
    if (gx < 0) { gw += gx; gx = 0; }
    if (gy < 0) { gh += gy; gy = 0; }
    if (gx + gw > LCD_Width)  gw = LCD_Width  - gx;
    if (gy + gh > LCD_Height) gh = LCD_Height - gy;
    if (gw <= 0 || gh <= 0) return;
    for (int y = 0; y < gh; y++)
    {
        uint16_t *row = &g_lcd_fb[(size_t)(gy + y) * LCD_Width + gx];
        for (int x = 0; x < gw; x++) row[x] = color;
    }
}

static void fb_copy_rect(int gx, int gy, int gw, int gh, const uint16_t *src)
{
    if (gw <= 0 || gh <= 0 || src == NULL) return;
    for (int y = 0; y < gh; y++)
    {
        int fy = gy + y;
        if (fy < 0 || fy >= LCD_Height) continue;
        const uint16_t *srow = src + (size_t)y * (size_t)gw;
        for (int x = 0; x < gw; x++)
        {
            int fx = gx + x;
            if (fx < 0 || fx >= LCD_Width) continue;
            g_lcd_fb[(size_t)fy * LCD_Width + fx] = srow[x];
        }
    }
}

static void oled_write_command(uint8_t lcd_command);
static void oled_write_data_8bit(uint8_t lcd_data);
static void oled_write_data_16bit(uint16_t lcd_data);
static void oled_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
static void oled_write_buffer(uint16_t *DataBuff, uint16_t DataSize);
static void oled_display_char(uint16_t x, uint16_t y,uint8_t c);
static void oled_display_chinese(uint16_t x, uint16_t y, char *pText);
static HAL_StatusTypeDef LCD_SPI_Transmit(SPI_HandleTypeDef *hspi,uint16_t pData, uint32_t Size);
static HAL_StatusTypeDef LCD_SPI_TransmitBuffer(SPI_HandleTypeDef *hspi, uint16_t *pData, uint32_t Size);

/* TXDR is declared as a 32 bit register, but the SPI FIFO takes one frame per
 * access and this panel runs in 16 bit mode, so a half-word store pushes
 * exactly one pixel while a word store pushes two.  Accessing the same
 * register through two widths is exactly what the hardware wants and exactly
 * what the C aliasing rules forbid, so the half-word view is declared
 * may_alias - the sanctioned way to tell GCC "this pun is deliberate" instead
 * of silencing -Wstrict-aliasing globally. */
typedef __IO uint16_t spi_txdr16_t __attribute__((may_alias));

#define SPI_TXDR16(h)   (*(spi_txdr16_t *)&(h)->Instance->TXDR)
#define SPI_TXDR32(h)   ((h)->Instance->TXDR)

//////////////////// Global function /////////////////////////
GlobalType_t driver_spi_oled_init(void)
{
    HAL_Delay(10);

    oled_write_command(0x36);
    oled_write_data_8bit(0x00);

    oled_write_command(0x3A);
    oled_write_data_8bit(0x05);

    // 接下来很多都是电压设置指令，直接使用厂家给设定值
    oled_write_command(0xB2);			
    oled_write_data_8bit(0x0C);
    oled_write_data_8bit(0x0C); 
    oled_write_data_8bit(0x00); 
    oled_write_data_8bit(0x33); 
    oled_write_data_8bit(0x33); 			

    oled_write_command(0xB7);
    oled_write_data_8bit(0x35);

    oled_write_command(0xBB);
    oled_write_data_8bit(0x19); 

    oled_write_command(0xC0);
    oled_write_data_8bit(0x2C);

    oled_write_command(0xC2);
    oled_write_data_8bit(0x01);

    oled_write_command(0xC3);		
    oled_write_data_8bit(0x12);
            
    oled_write_command(0xC4);
    oled_write_data_8bit(0x20); 

    oled_write_command(0xC6);
    oled_write_data_8bit(0x0F);

    oled_write_command(0xD0);
    oled_write_data_8bit(0xA4);
    oled_write_data_8bit(0xA1);

    oled_write_command(0xE0);
    oled_write_data_8bit(0xD0);
    oled_write_data_8bit(0x04);
    oled_write_data_8bit(0x0D);
    oled_write_data_8bit(0x11);
    oled_write_data_8bit(0x13);
    oled_write_data_8bit(0x2B);
    oled_write_data_8bit(0x3F);
    oled_write_data_8bit(0x54);
    oled_write_data_8bit(0x4C);
    oled_write_data_8bit(0x18);
    oled_write_data_8bit(0x0D);
    oled_write_data_8bit(0x0B);
    oled_write_data_8bit(0x1F);
    oled_write_data_8bit(0x23);

    oled_write_command(0xE1);      // 负极电压伽马值设定
    oled_write_data_8bit(0xD0);
    oled_write_data_8bit(0x04);
    oled_write_data_8bit(0x0C);
    oled_write_data_8bit(0x11);
    oled_write_data_8bit(0x13);
    oled_write_data_8bit(0x2C);
    oled_write_data_8bit(0x3F);
    oled_write_data_8bit(0x44);
    oled_write_data_8bit(0x51);
    oled_write_data_8bit(0x2F);
    oled_write_data_8bit(0x1F);
    oled_write_data_8bit(0x1F);
    oled_write_data_8bit(0x20);
    oled_write_data_8bit(0x23);

    oled_write_command(0x21); 

    // 退出休眠指令，
    oled_write_command(0x11);
    HAL_Delay(120);

    // 打开显示指令
    oled_write_command(0x29);

    // 以下进行一些驱动的默认设置
    LCD_SetDirection(Direction_V);  	   // 设置显示方向
    LCD_SetBackColor(LCD_BLACK);           // 设置背景色
    LCD_SetColor(LCD_WHITE);               // 设置画笔色  
    LCD_Clear();                           // 清屏

    LCD_SetAsciiFont(&ASCII_Font24);       // 设置默认字体
    LCD_ShowNumMode(Fill_Zero);	      	   // 设置变量显示模式，多余位填充空格还是填充0

    // 全部设置完毕之后，打开背光	
    LCD_BL_ON();
    
    return RT_OK;
}

void LCD_SetAsciiFont(pFONT *Asciifonts)
{
    LCD_AsciiFonts = Asciifonts;
}

void LCD_ShowNumMode(uint8_t mode)
{
	g_oled_info.ShowNum_Mode = mode;
}

void LCD_SetDirection(uint8_t direction)
{
    g_oled_info.Direction = direction;

    // 横屏显示
    if( direction == Direction_H )
    {
        oled_write_command(0x36);
        oled_write_data_8bit(0x70);
        g_oled_info.X_Offset   = 0;
        g_oled_info.Y_Offset   = 0;   
        g_oled_info.Width      = LCD_Height;
        g_oled_info.Height     = LCD_Width;		
    }
    // 垂直显示
    else if( direction == Direction_V )
    {
        oled_write_command(0x36);
        oled_write_data_8bit(0x00);
        g_oled_info.X_Offset   = 0;
        g_oled_info.Y_Offset   = 0;     
        g_oled_info.Width      = LCD_Width;
        g_oled_info.Height     = LCD_Height;						
    }
    // 横屏显示，并上下翻转
    else if( direction == Direction_H_Flip )
    {
        oled_write_command(0x36);
        oled_write_data_8bit(0xA0);         
        g_oled_info.X_Offset   = 80;
        g_oled_info.Y_Offset   = 0;      
        g_oled_info.Width      = LCD_Height;
        g_oled_info.Height     = LCD_Width;				
    }
    // 垂直显示 ，并上下翻转
    else if( direction == Direction_V_Flip )
    {
        oled_write_command(0x36);
        oled_write_data_8bit(0xC0);        
        g_oled_info.X_Offset   = 0;
        g_oled_info.Y_Offset   = 80;     
        g_oled_info.Width      = LCD_Width;
        g_oled_info.Height     = LCD_Height;				
    }   
}

void LCD_SetBackColor(uint32_t Color)
{
	uint16_t Red_Value = 0;
    uint16_t Green_Value = 0;
    uint16_t Blue_Value = 0;

    // 转换成16位的RGB565颜色
	Red_Value = (uint16_t)((Color&0x00F80000)>>8);   
	Green_Value = (uint16_t)((Color&0x0000FC00)>>5);
	Blue_Value = (uint16_t)((Color&0x000000F8)>>3);

	g_oled_info.BackColor = (uint16_t)(Red_Value | Green_Value | Blue_Value);	   	
}

void LCD_SetColor(uint32_t Color)
{
	uint16_t Red_Value = 0;
    uint16_t Green_Value = 0;
    uint16_t Blue_Value = 0;

    // 转换成16位的RGB565颜色
	Red_Value = (uint16_t)((Color&0x00F80000)>>8);
	Green_Value = (uint16_t)((Color&0x0000FC00)>>5);
	Blue_Value = (uint16_t)((Color&0x000000F8)>>3);

	g_oled_info.Color = (uint16_t)(Red_Value | Green_Value | Blue_Value);	
}

void LCD_Clear(void)
{
    oled_set_address(0, 0, g_oled_info.Width-1, g_oled_info.Height-1);

    LCD_DC_DATA();

    LCD_SPI.Init.DataSize 	= SPI_DATASIZE_16BIT;
    HAL_SPI_Init(&LCD_SPI);		

    LCD_SPI_Transmit(&LCD_SPI, g_oled_info.BackColor, g_oled_info.Width * g_oled_info.Height);

    LCD_SPI.Init.DataSize 	= SPI_DATASIZE_8BIT;
    HAL_SPI_Init(&LCD_SPI);

    /* mirror the whole-panel clear into the shadow framebuffer */
    fb_fill_rect(g_oled_info.X_Offset, g_oled_info.Y_Offset,
                 (int)g_oled_info.Width, (int)g_oled_info.Height,
                 g_oled_info.BackColor);
}

void LCD_SetTextFont(pFONT *fonts)
{
	LCD_CHFonts = fonts;
    
	switch(fonts->Width )
	{
        // 设置ASCII字符的字体为1206
		case 12:	
            LCD_AsciiFonts = &ASCII_Font12;	
        break;
        // 设置ASCII字符的字体为1608
		case 16:	
            LCD_AsciiFonts = &ASCII_Font16;	
        break;
        // 设置ASCII字符的字体为2010	
		case 20:	
            LCD_AsciiFonts = &ASCII_Font20;	
        break;
        // 设置ASCII字符的字体为 2412
		case 24:	
            LCD_AsciiFonts = &ASCII_Font24;	
        break;
        // 设置ASCII字符的字体为 3216	
		case 32:	
            LCD_AsciiFonts = &ASCII_Font32;	
        break;		
		default: 
        break;
	}
}

/**
  * @brief  Draw a mixed ASCII / GBK string.
  * @note   Bytes <= 0x7F are ASCII and consume one byte; anything else is
  *         treated as the lead byte of a two byte GBK character.  Source files
  *         must therefore be encoded GBK, not UTF-8 (the CMake build passes
  *         -finput-charset=UTF-8 -fexec-charset=GBK so UTF-8 sources are
  *         transcoded automatically at compile time).
  */
void LCD_DisplayText(uint16_t x, uint16_t y, char *pText)
{
    if (pText == NULL)
    {
        return;
    }

    while (*pText != '\0')
    {
        if ((uint8_t)*pText <= 0x7F)
        {
            if (LCD_AsciiFonts == NULL)
            {
                return;
            }
            /* Stop before running off the right edge */
            if (x + LCD_AsciiFonts->Width > g_oled_info.Width)
            {
                return;
            }
            oled_display_char(x, y, (uint8_t)*pText);
            x += LCD_AsciiFonts->Width;
            pText++;
        }
        else
        {
            if (LCD_CHFonts == NULL || pText[1] == '\0')
            {
                return;
            }
            if (x + LCD_CHFonts->Width > g_oled_info.Width)
            {
                return;
            }
            oled_display_chinese(x, y, pText);
            x += LCD_CHFonts->Width;
            pText += 2;
        }
    }
}

/**
  * @brief  Draw a right aligned integer padded to len digits.
  */
void LCD_DisplayNumber(uint16_t x, uint16_t y, int32_t number, uint8_t len)
{
    char    buf[16];
    char    pad = (g_oled_info.ShowNum_Mode == Fill_Zero) ? '0' : ' ';
    uint8_t i;
    int32_t value = number;
    uint8_t digits = 0;
    uint8_t neg = 0;

    if (len == 0 || len >= sizeof(buf))
    {
        return;
    }

    if (value < 0)
    {
        neg = 1;
        value = -value;
    }

    /* Render digits back to front */
    memset(buf, pad, len);
    buf[len] = '\0';

    i = len;
    do
    {
        if (i == 0)
        {
            break;
        }
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
        digits++;
    } while (value != 0);

    if (neg && i > 0)
    {
        buf[--i] = '-';
    }

    (void)digits;
    LCD_DisplayText(x, y, buf);
}

/**
  * @brief  Flood fill a rectangle with a solid colour.
  */
void LCD_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t Color)
{
    uint16_t rgb565;
    uint16_t r, g, b;

    if (width == 0 || height == 0)
    {
        return;
    }
    if (x >= g_oled_info.Width || y >= g_oled_info.Height)
    {
        return;
    }
    /* Clip to the panel */
    if (x + width  > g_oled_info.Width)  { width  = g_oled_info.Width  - x; }
    if (y + height > g_oled_info.Height) { height = g_oled_info.Height - y; }

    r = (uint16_t)((Color & 0x00F80000) >> 8);
    g = (uint16_t)((Color & 0x0000FC00) >> 5);
    b = (uint16_t)((Color & 0x000000F8) >> 3);
    rgb565 = (uint16_t)(r | g | b);

    oled_set_address(x, y, x + width - 1, y + height - 1);

    LCD_DC_DATA();

    LCD_SPI.Init.DataSize = SPI_DATASIZE_16BIT;
    HAL_SPI_Init(&LCD_SPI);

    LCD_SPI_Transmit(&LCD_SPI, rgb565, (uint32_t)width * height);

    LCD_SPI.Init.DataSize = SPI_DATASIZE_8BIT;
    HAL_SPI_Init(&LCD_SPI);

    /* mirror the filled rectangle into the shadow framebuffer */
    fb_fill_rect((int)x + g_oled_info.X_Offset, (int)y + g_oled_info.Y_Offset,
                 (int)width, (int)height, rgb565);
}

void LCD_CopyBuffer(uint16_t x, uint16_t y,uint16_t width,uint16_t height,uint16_t *DataBuff)
{
    oled_set_address(x,y,x+width-1,y+height-1);

    LCD_DC_DATA();

    // 修改为16位数据宽度
    LCD_SPI.Init.DataSize 	= SPI_DATASIZE_16BIT;
    HAL_SPI_Init(&LCD_SPI);		

    LCD_SPI_TransmitBuffer(&LCD_SPI, DataBuff, width * height) ;

    // 改回8位数据宽度
    LCD_SPI.Init.DataSize 	= SPI_DATASIZE_8BIT;
    HAL_SPI_Init(&LCD_SPI);		

    /* mirror the copied rectangle into the shadow framebuffer */
    fb_copy_rect((int)x + g_oled_info.X_Offset, (int)y + g_oled_info.Y_Offset,
                 (int)width, (int)height, DataBuff);
}

/*---------------------------------------------------------------------------
 *  Screenshot accessors
 *-------------------------------------------------------------------------*/
const uint16_t *LCD_GetFrameBuffer(void)
{
    return g_lcd_fb;
}

void LCD_GetResolution(int *w, int *h)
{
    if (w != NULL) *w = LCD_Width;
    if (h != NULL) *h = LCD_Height;
}

//////////////////// internal function /////////////////////////
static void oled_display_char(uint16_t x, uint16_t y,uint8_t c)
{
    uint16_t index = 0, counter = 0 ,i = 0, w = 0;
    uint8_t disChar;

    /* The ASCII tables start at 0x20; anything below prints as a space */
    if (c < 32)
    {
        c = 32;
    }
    c = c - 32;

    for(index = 0; index < LCD_AsciiFonts->Sizes; index++)	
    {
        disChar = LCD_AsciiFonts->pTable[c*LCD_AsciiFonts->Sizes + index];
        for(counter = 0; counter < 8; counter++)
        { 
            if(disChar & 0x01)	
            {		
                LCD_Buff[i] = g_oled_info.Color;
            }
            else		
            {		
                LCD_Buff[i] = g_oled_info.BackColor;
            }
            disChar >>= 1;
            i++;
            w++;
            if( w == LCD_AsciiFonts->Width )
            {
                w = 0;
                break;
            }        
        }	
    }		
    oled_set_address( x, y, x+LCD_AsciiFonts->Width-1, y+LCD_AsciiFonts->Height-1);
    oled_write_buffer(LCD_Buff,LCD_AsciiFonts->Width*LCD_AsciiFonts->Height);

    /* mirror the glyph into the shadow framebuffer */
    fb_copy_rect((int)x + g_oled_info.X_Offset, (int)y + g_oled_info.Y_Offset,
                 (int)LCD_AsciiFonts->Width, (int)LCD_AsciiFonts->Height, LCD_Buff);
}

static uint8_t dzk[192] = {0};
static void oled_display_chinese(uint16_t x, uint16_t y, char *pText) 
{
    uint16_t  i = 0, index = 0, counter = 0;
    uint16_t  addr = 0;
    uint8_t   disChar;
    uint16_t  Xaddress = 0;  
    const uint8_t *pstart;
    
    // 使用内部的中文字库
    if (LCD_CHFonts->pTable != NULL)
    {
        while(1)
        {		
            if( *(LCD_CHFonts->pTable + (i+1)*LCD_CHFonts->Sizes + 0)==*pText 
            && *(LCD_CHFonts->pTable + (i+1)*LCD_CHFonts->Sizes + 1)==*(pText+1) )	
            {   
                addr=i;
                break;
            }				
            i+=2;

            if(i >= LCD_CHFonts->Table_Rows)	
                break;
        }
        pstart = LCD_CHFonts->pTable + (addr)*LCD_CHFonts->Sizes;
    }
    else  // 使用GBK字库
    {
        lcd_driver_get_hzmat((uint8_t *)pText, dzk, LCD_CHFonts);

        pstart = dzk;
    }
    
    i=0;
    for(index = 0; index <LCD_CHFonts->Sizes; index++)
    {	
        disChar = *(pstart + index);

        for(counter = 0; counter < 8; counter++)
        { 
            if(disChar & 0x01)	
            {		
                LCD_Buff[i] =  g_oled_info.Color;
            }
            else		
            {		
                LCD_Buff[i] = g_oled_info.BackColor;
            }
            i++;
            disChar >>= 1;
            Xaddress++;
            
            if( Xaddress == LCD_CHFonts->Width )
            {
                Xaddress = 0;
                break;
            }
        }	
    }
    oled_set_address(x, y, x+LCD_CHFonts->Width-1, y+LCD_CHFonts->Height-1);
    oled_write_buffer(LCD_Buff, LCD_CHFonts->Width*LCD_CHFonts->Height);

    /* mirror the glyph into the shadow framebuffer */
    fb_copy_rect((int)x + g_oled_info.X_Offset, (int)y + g_oled_info.Y_Offset,
                 (int)LCD_CHFonts->Width, (int)LCD_CHFonts->Height, LCD_Buff);
}

static void oled_write_command(uint8_t lcd_command)
{
   // 输出指令
   LCD_DC_COMMAND();

   HAL_SPI_Transmit(&LCD_SPI, &lcd_command, 1, 1000);
}

static void oled_write_data_8bit(uint8_t lcd_data)
{
   LCD_DC_DATA();

   HAL_SPI_Transmit(&LCD_SPI, &lcd_data, 1, 1000);
}

static void oled_write_data_16bit(uint16_t lcd_data)
{
   uint8_t lcd_data_buff[2];
    
   LCD_DC_DATA();
 
   lcd_data_buff[0] = lcd_data>>8;
   lcd_data_buff[1] = lcd_data;
		
	HAL_SPI_Transmit(&LCD_SPI, lcd_data_buff, 2, 1000);
}

static void oled_write_buffer(uint16_t *DataBuff, uint16_t DataSize)
{
    LCD_DC_DATA();

    // 切换到16bit位宽
    LCD_SPI.Init.DataSize 	= SPI_DATASIZE_16BIT; 
    HAL_SPI_Init(&LCD_SPI);		

    HAL_SPI_Transmit(&LCD_SPI, (uint8_t *)DataBuff, DataSize, 1000);

    // 切换到8bit位宽
    LCD_SPI.Init.DataSize 	= SPI_DATASIZE_8BIT;
    HAL_SPI_Init(&LCD_SPI);	
}

static void oled_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)		
{
    // 列地址设置，即X坐标
	oled_write_command(0x2a);
	oled_write_data_16bit(x1 + g_oled_info.X_Offset);
	oled_write_data_16bit(x2 + g_oled_info.X_Offset);

    // 行地址设置，即Y坐标
	oled_write_command(0x2b);
	oled_write_data_16bit(y1 + g_oled_info.Y_Offset);
	oled_write_data_16bit(y2 + g_oled_info.Y_Offset);

    // 开始写入显存，即要显示的颜色数据
	oled_write_command(0x2c);
}

/**
  * @brief Handle SPI Communication Timeout.
  * @param hspi: pointer to a SPI_HandleTypeDef structure that contains
  *              the configuration information for SPI module.
  * @param Flag: SPI flag to check
  * @param Status: flag state to check
  * @param Timeout: Timeout duration
  * @param Tickstart: Tick start value
  * @retval HAL status
  */
static HAL_StatusTypeDef LCD_SPI_WaitOnFlagUntilTimeout(SPI_HandleTypeDef *hspi, uint32_t Flag, FlagStatus Status,
                                                    uint32_t Tickstart, uint32_t Timeout)
{
   /* Wait until flag is set */
   while ((__HAL_SPI_GET_FLAG(hspi, Flag) ? SET : RESET) == Status)
   {
      /* Check for the Timeout */
      if ((((HAL_GetTick() - Tickstart) >=  Timeout) && (Timeout != HAL_MAX_DELAY)) || (Timeout == 0U))
      {
         return HAL_TIMEOUT;
      }
   }
   return HAL_OK;
}


/**
 * @brief  Close Transfer and clear flags.
 * @param  hspi: pointer to a SPI_HandleTypeDef structure that contains
 *               the configuration information for SPI module.
 * @retval HAL_ERROR: if any error detected
 *         HAL_OK: if nothing detected
 */
static void LCD_SPI_CloseTransfer(SPI_HandleTypeDef *hspi)
{
  uint32_t itflag = hspi->Instance->SR;

  __HAL_SPI_CLEAR_EOTFLAG(hspi);
  __HAL_SPI_CLEAR_TXTFFLAG(hspi);

  /* Disable SPI peripheral */
  __HAL_SPI_DISABLE(hspi);

  /* Disable ITs */
  __HAL_SPI_DISABLE_IT(hspi, (SPI_IT_EOT | SPI_IT_TXP | SPI_IT_RXP | SPI_IT_DXP | SPI_IT_UDR | SPI_IT_OVR | SPI_IT_FRE | SPI_IT_MODF));

  /* Disable Tx DMA Request */
  CLEAR_BIT(hspi->Instance->CFG1, SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN);

  /* Report UnderRun error for non RX Only communication */
  if (hspi->State != HAL_SPI_STATE_BUSY_RX)
  {
    if ((itflag & SPI_FLAG_UDR) != 0UL)
    {
      SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_UDR);
      __HAL_SPI_CLEAR_UDRFLAG(hspi);
    }
  }

  /* Report OverRun error for non TX Only communication */
  if (hspi->State != HAL_SPI_STATE_BUSY_TX)
  {
    if ((itflag & SPI_FLAG_OVR) != 0UL)
    {
      SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_OVR);
      __HAL_SPI_CLEAR_OVRFLAG(hspi);
    }
  }

  /* SPI Mode Fault error interrupt occurred -------------------------------*/
  if ((itflag & SPI_FLAG_MODF) != 0UL)
  {
    SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_MODF);
    __HAL_SPI_CLEAR_MODFFLAG(hspi);
  }

  /* SPI Frame error interrupt occurred ------------------------------------*/
  if ((itflag & SPI_FLAG_FRE) != 0UL)
  {
    SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_FRE);
    __HAL_SPI_CLEAR_FREFLAG(hspi);
  }

  hspi->TxXferCount = (uint16_t)0UL;
  hspi->RxXferCount = (uint16_t)0UL;
}

static HAL_StatusTypeDef LCD_SPI_Transmit(SPI_HandleTypeDef *hspi,uint16_t pData, uint32_t Size)
{
   uint32_t    tickstart;  
   uint32_t    Timeout = 1000;      // 超时判断
   uint32_t    LCD_pData_32bit;     // 按32位传输时的数据
   uint32_t    LCD_TxDataCount;     // 传输计数
   HAL_StatusTypeDef errorcode = HAL_OK;

	/* Check Direction parameter */
	assert_param(IS_SPI_DIRECTION_2LINES_OR_1LINE_2LINES_TXONLY(hspi->Init.Direction));

	/* Process Locked */
	__HAL_LOCK(hspi);

	/* Init tickstart for timeout management*/
	tickstart = HAL_GetTick();

	if (hspi->State != HAL_SPI_STATE_READY)
	{
		errorcode = HAL_BUSY;
		__HAL_UNLOCK(hspi);
		return errorcode;
	}

	if ( Size == 0UL)
	{
		errorcode = HAL_ERROR;
		__HAL_UNLOCK(hspi);
		return errorcode;
	}

	/* Set the transaction information */
	hspi->State       = HAL_SPI_STATE_BUSY_TX;
	hspi->ErrorCode   = HAL_SPI_ERROR_NONE;

	LCD_TxDataCount   = Size;                // 传输的数据长度
	LCD_pData_32bit   = (pData<<16)|pData ;  // 按32位传输时，合并2个像素点的颜色  

	/*Init field not used in handle to zero */
	hspi->pRxBuffPtr  = NULL;
	hspi->RxXferSize  = (uint16_t) 0UL;
	hspi->RxXferCount = (uint16_t) 0UL;
	hspi->TxISR       = NULL;
	hspi->RxISR       = NULL;

	/* Configure communication direction : 1Line */
	if (hspi->Init.Direction == SPI_DIRECTION_1LINE)
	{
		SPI_1LINE_TX(hspi);
	}

// 不使用硬件 TSIZE 控制，此处设置为0，即不限制传输的数据长度
	MODIFY_REG(hspi->Instance->CR2, SPI_CR2_TSIZE, 0);

	/* Enable SPI peripheral */
	__HAL_SPI_ENABLE(hspi);

	if (hspi->Init.Mode == SPI_MODE_MASTER)
	{
		 /* Master transfer start */
		 SET_BIT(hspi->Instance->CR1, SPI_CR1_CSTART);
	}

	/* Transmit data in 16 Bit mode */
	while (LCD_TxDataCount > 0UL)
	{
		/* Wait until TXP flag is set to send data */
		if (__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_TXP))
		{
			if ((hspi->TxXferCount > 1UL) && (hspi->Init.FifoThreshold > SPI_FIFO_THRESHOLD_01DATA))
			{
				SPI_TXDR32(hspi) = (uint32_t)LCD_pData_32bit;
				LCD_TxDataCount -= (uint16_t)2UL;
			}
			else
			{
				/* TXDR is a 32 bit register but a 16 bit access pushes exactly
				 * one frame into the FIFO; the (void *) hop keeps the compiler
				 * from complaining about the (legitimate) MMIO type pun. */
				SPI_TXDR16(hspi) = (uint16_t)pData;
				LCD_TxDataCount--;
			}
		}
		else
		{
			/* Timeout management */
			if ((((HAL_GetTick() - tickstart) >=  Timeout) && (Timeout != HAL_MAX_DELAY)) || (Timeout == 0U))
			{
				/* Call standard close procedure with error check */
				LCD_SPI_CloseTransfer(hspi);

				/* Process Unlocked */
				__HAL_UNLOCK(hspi);

				SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_TIMEOUT);
				hspi->State = HAL_SPI_STATE_READY;
				return HAL_ERROR;
			}
		}
	}

	if (LCD_SPI_WaitOnFlagUntilTimeout(hspi, SPI_SR_TXC, RESET, tickstart, Timeout) != HAL_OK)
	{
		SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_FLAG);
	}

	SET_BIT((hspi)->Instance->CR1 , SPI_CR1_CSUSP); // 请求挂起SPI传输
	/* 等待SPI挂起 */
	if (LCD_SPI_WaitOnFlagUntilTimeout(hspi, SPI_FLAG_SUSP, RESET, tickstart, Timeout) != HAL_OK)
	{
		SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_FLAG);
	}
	LCD_SPI_CloseTransfer(hspi);   /* Call standard close procedure with error check */

	SET_BIT((hspi)->Instance->IFCR , SPI_IFCR_SUSPC);  // 清除挂起标志位


	/* Process Unlocked */
	__HAL_UNLOCK(hspi);

	hspi->State = HAL_SPI_STATE_READY;

	if (hspi->ErrorCode != HAL_SPI_ERROR_NONE)
	{
		return HAL_ERROR;
	}
	return errorcode;
}

static HAL_StatusTypeDef LCD_SPI_TransmitBuffer(SPI_HandleTypeDef *hspi, uint16_t *pData, uint32_t Size)
{
   uint32_t    tickstart;  
   uint32_t    Timeout = 1000;      // 超时判断
   uint32_t    LCD_TxDataCount;     // 传输计数
   HAL_StatusTypeDef errorcode = HAL_OK;

	/* Check Direction parameter */
	assert_param(IS_SPI_DIRECTION_2LINES_OR_1LINE_2LINES_TXONLY(hspi->Init.Direction));

	/* Process Locked */
	__HAL_LOCK(hspi);

	/* Init tickstart for timeout management*/
	tickstart = HAL_GetTick();

	if (hspi->State != HAL_SPI_STATE_READY)
	{
		errorcode = HAL_BUSY;
		__HAL_UNLOCK(hspi);
		return errorcode;
	}

	if ( Size == 0UL)
	{
		errorcode = HAL_ERROR;
		__HAL_UNLOCK(hspi);
		return errorcode;
	}

	/* Set the transaction information */
	hspi->State       = HAL_SPI_STATE_BUSY_TX;
	hspi->ErrorCode   = HAL_SPI_ERROR_NONE;

	LCD_TxDataCount   = Size;                // 传输的数据长度

	/*Init field not used in handle to zero */
	hspi->pRxBuffPtr  = NULL;
	hspi->RxXferSize  = (uint16_t) 0UL;
	hspi->RxXferCount = (uint16_t) 0UL;
	hspi->TxISR       = NULL;
	hspi->RxISR       = NULL;

	/* Configure communication direction : 1Line */
	if (hspi->Init.Direction == SPI_DIRECTION_1LINE)
	{
		SPI_1LINE_TX(hspi);
	}

// 不使用硬件 TSIZE 控制，此处设置为0，即不限制传输的数据长度
	MODIFY_REG(hspi->Instance->CR2, SPI_CR2_TSIZE, 0);

	/* Enable SPI peripheral */
	__HAL_SPI_ENABLE(hspi);

	if (hspi->Init.Mode == SPI_MODE_MASTER)
	{
		 /* Master transfer start */
		 SET_BIT(hspi->Instance->CR1, SPI_CR1_CSTART);
	}

	/* Transmit data in 16 Bit mode */
	while (LCD_TxDataCount > 0UL)
	{
		/* Wait until TXP flag is set to send data */
		if (__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_TXP))
		{
			if ((LCD_TxDataCount > 1UL) && (hspi->Init.FifoThreshold > SPI_FIFO_THRESHOLD_01DATA))
			{
				/* Two pixels per FIFO write.  memcpy instead of a uint32_t
				 * cast: the source is a uint16_t array, so the cast would be
				 * both an aliasing violation and a potential unaligned load.
				 * GCC folds this into a single LDR. */
				uint32_t pair;
				__builtin_memcpy(&pair, pData, sizeof(pair));
				SPI_TXDR32(hspi) = pair;
				pData += 2;
				LCD_TxDataCount -= 2;
			}
			else
			{
				SPI_TXDR16(hspi) = *pData;
				pData += 1;
				LCD_TxDataCount--;
			}
		}
		else
		{
			/* Timeout management */
			if ((((HAL_GetTick() - tickstart) >=  Timeout) && (Timeout != HAL_MAX_DELAY)) || (Timeout == 0U))
			{
				/* Call standard close procedure with error check */
				LCD_SPI_CloseTransfer(hspi);

				/* Process Unlocked */
				__HAL_UNLOCK(hspi);

				SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_TIMEOUT);
				hspi->State = HAL_SPI_STATE_READY;
				return HAL_ERROR;
			}
		}
	}

	if (LCD_SPI_WaitOnFlagUntilTimeout(hspi, SPI_SR_TXC, RESET, tickstart, Timeout) != HAL_OK)
	{
		SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_FLAG);
	}

	SET_BIT((hspi)->Instance->CR1 , SPI_CR1_CSUSP); // 请求挂起SPI传输
	/* 等待SPI挂起 */
	if (LCD_SPI_WaitOnFlagUntilTimeout(hspi, SPI_FLAG_SUSP, RESET, tickstart, Timeout) != HAL_OK)
	{
		SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_FLAG);
	}
	LCD_SPI_CloseTransfer(hspi);   /* Call standard close procedure with error check */

	SET_BIT((hspi)->Instance->IFCR , SPI_IFCR_SUSPC);  // 清除挂起标志位


	/* Process Unlocked */
	__HAL_UNLOCK(hspi);

	hspi->State = HAL_SPI_STATE_READY;

	if (hspi->ErrorCode != HAL_SPI_ERROR_NONE)
	{
		return HAL_ERROR;
	}
	return errorcode;
}
