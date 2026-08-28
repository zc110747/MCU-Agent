/**
  ******************************************************************************
  * @file    bsp_lcd.c
  * @brief   FMC Bank1 NE1 8080 16-bit LCD driver for the STM32F429 Apollo board.
  *
  *  Adapted from embedded_based_on_stm32/code/00-Drivers/drv_lcd.c:
  *   - F429 FMC GPIO pin configuration added (the reference relied on an
  *     external HAL_SRAM_MspInit that does not exist in this project).
  *   - NT35510 init sequence kept verbatim; an ILI9806E generic MIPI-DCS
  *     fallback added (board may ship either controller).
  *   - lcd_color_fill() added for the LVGL display port.
  *   - Resolution is taken from LCD_WIDTH / LCD_HEIGHT (real panel 800x480).
  ******************************************************************************
  */
#include <string.h>
#include <stdio.h>
#include "bsp_lcd.h"

/* -------------------------------------------------------------------------- */
/* Local state                                                                */
/* -------------------------------------------------------------------------- */
static SRAM_HandleTypeDef hsram1;
LCD_INFO g_lcd_info = {0};

/* Set once lcd_config_init() has published the final GRAM window.  Other
 * threads (the touch service, which maps coordinates onto that window) must
 * not read g_lcd_info before this is set: until then the width/height are
 * still the pre-scan-direction values. */
static uint8_t g_lcd_ready = 0U;

/* scan direction */
#define L2R_U2D  0
#define L2R_D2U  1
#define R2L_U2D  2
#define R2L_D2U  3
#define U2D_L2R  4
#define U2D_R2L  5
#define D2U_L2R  6
#define D2U_R2L  7
#define DFT_SCAN_DIR  L2R_U2D

/* local function prototypes */
static void lcd_config_init(void);
static GlobalType_t lcd_hardware_init(void);
static uint16_t lcd_rd_data(void);
static void lcd_write_reg(uint16_t regval);
static void lcd_write_data(uint16_t data);
static void lcd_write_reg_data(uint16_t reg, uint16_t data);
static void lcd_display_dir(uint8_t dir);
static void lcd_scan_dir(uint8_t dir);

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
GlobalType_t lcd_driver_init(void)
{
    GlobalType_t result;

    memset((char *)&g_lcd_info, 0, sizeof(LCD_INFO));

    g_lcd_info.lcd_width  = LCD_WIDTH;
    g_lcd_info.lcd_height = LCD_HEIGHT;
    g_lcd_info.wramcmd    = 0x2C00;
    g_lcd_info.setxcmd    = 0x2A00;
    g_lcd_info.setycmd    = 0x2B00;

    result = lcd_hardware_init();
    if (result == RT_OK)
    {
        HAL_Delay(20);
        lcd_config_init();
    }
    else
    {
        printf("[LCD ] FMC SRAM init FAILED\r\n");
    }
    return result;
}

LCD_INFO *get_lcd_info(void)
{
    return &g_lcd_info;
}

/**
  * @brief  @retval 1 once the panel is up and g_lcd_info holds the final GRAM
  *         window (i.e. after the scan-direction swap has been applied).
  */
int lcd_driver_ready(void)
{
    return (g_lcd_ready != 0U) ? 1 : 0;
}

void lcd_driver_clear(uint32_t color)
{
    uint32_t index = 0;
    uint32_t totalpoint = g_lcd_info.lcd_width;

    totalpoint *= g_lcd_info.lcd_height;
    lcd_set_cursor(0x0, 0x0);
    lcd_write_ram_prepare();
    for (index = 0; index < totalpoint; index++)
    {
        LCD->LCD_RAM = color;
    }
}

void lcd_set_cursor(uint16_t Xpos, uint16_t Ypos)
{
    lcd_write_reg(g_lcd_info.setxcmd);
    lcd_write_data(Xpos >> 8);
    lcd_write_reg(g_lcd_info.setxcmd + 1);
    lcd_write_data(Xpos & 0xFF);
    lcd_write_reg(g_lcd_info.setycmd);
    lcd_write_data(Ypos >> 8);
    lcd_write_reg(g_lcd_info.setycmd + 1);
    lcd_write_data(Ypos & 0xFF);
}

void lcd_write_ram_prepare(void)
{
    LCD->LCD_REG = g_lcd_info.wramcmd;
    __NOP();
}

void lcd_fast_drawpoint(uint16_t x, uint16_t y, uint32_t color)
{
    lcd_write_reg(g_lcd_info.setxcmd);
    lcd_write_data(x >> 8);
    lcd_write_reg(g_lcd_info.setxcmd + 1);
    lcd_write_data(x & 0xFF);
    lcd_write_reg(g_lcd_info.setycmd);
    lcd_write_data(y >> 8);
    lcd_write_reg(g_lcd_info.setycmd + 1);
    lcd_write_data(y & 0xFF);

    LCD->LCD_REG = g_lcd_info.wramcmd;
    LCD->LCD_RAM = color;
}

void lcd_color_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *pixels)
{
    uint32_t i;
    uint32_t total = (uint32_t)w * h;

    if (pixels == NULL || w == 0 || h == 0)
    {
        return;
    }

    /* column address window */
    lcd_write_reg(g_lcd_info.setxcmd);
    lcd_write_data(x >> 8);
    lcd_write_reg(g_lcd_info.setxcmd + 1);
    lcd_write_data(x & 0xFF);
    lcd_write_reg(g_lcd_info.setxcmd + 2);
    lcd_write_data((x + w - 1) >> 8);
    lcd_write_reg(g_lcd_info.setxcmd + 3);
    lcd_write_data((x + w - 1) & 0xFF);

    /* row address window */
    lcd_write_reg(g_lcd_info.setycmd);
    lcd_write_data(y >> 8);
    lcd_write_reg(g_lcd_info.setycmd + 1);
    lcd_write_data(y & 0xFF);
    lcd_write_reg(g_lcd_info.setycmd + 2);
    lcd_write_data((y + h - 1) >> 8);
    lcd_write_reg(g_lcd_info.setycmd + 3);
    lcd_write_data((y + h - 1) & 0xFF);

    lcd_write_ram_prepare();
    for (i = 0; i < total; i++)
    {
        LCD->LCD_RAM = pixels[i];
    }
}

/* -------------------------------------------------------------------------- */
/* Local helpers                                                              */
/* -------------------------------------------------------------------------- */
static void lcd_delay_us(uint16_t times)
{
    uint16_t i, j;
    for (i = 0; i < times; i++)
    {
        for (j = 0; j < 5; j++)
        {
            __NOP();
        }
    }
}

static GlobalType_t lcd_hardware_init(void)
{
    FMC_NORSRAM_TimingTypeDef Timing = {0};
    FMC_NORSRAM_TimingTypeDef ExtTiming = {0};
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Backlight off while we bring the interface up. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Configure the FMC Bank1 NE1 8080 16-bit data/address/control pins as
     * alternate-function 12.  The data bus (PD/PE) and address lines (PF/PG,
     * PD11..13) are shared with the SDRAM controller on this board, so
     * re-initialising them as FMC AF here is idempotent and safe. */
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

    /* PD: D0,D1,D2,D3,NOE,NWE,NE1,D13,D14,D15,A16,A17,A18 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
                          GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                          GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
                          GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* PE: D4..D12 */
    GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                          GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
                          GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* PF: A0..A5, A6..A9 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                          GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_12 | GPIO_PIN_13 |
                          GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    /* PG: A10..A15 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                          GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    hsram1.Instance = FMC_NORSRAM_DEVICE;
    hsram1.Extended = FMC_NORSRAM_EXTENDED_DEVICE;
    hsram1.Init.NSBank = FMC_NORSRAM_BANK1;
    hsram1.Init.DataAddressMux = FMC_DATA_ADDRESS_MUX_DISABLE;
    hsram1.Init.MemoryType = FMC_MEMORY_TYPE_SRAM;
    hsram1.Init.MemoryDataWidth = FMC_NORSRAM_MEM_BUS_WIDTH_16;
    hsram1.Init.BurstAccessMode = FMC_BURST_ACCESS_MODE_DISABLE;
    hsram1.Init.WaitSignalPolarity = FMC_WAIT_SIGNAL_POLARITY_LOW;
    hsram1.Init.WrapMode = FMC_WRAP_MODE_DISABLE;
    hsram1.Init.WaitSignalActive = FMC_WAIT_TIMING_BEFORE_WS;
    hsram1.Init.WriteOperation = FMC_WRITE_OPERATION_ENABLE;
    hsram1.Init.WaitSignal = FMC_WAIT_SIGNAL_DISABLE;
    hsram1.Init.ExtendedMode = FMC_EXTENDED_MODE_ENABLE;
    hsram1.Init.AsynchronousWait = FMC_ASYNCHRONOUS_WAIT_DISABLE;
    hsram1.Init.WriteBurst = FMC_WRITE_BURST_DISABLE;
    hsram1.Init.ContinuousClock = FMC_CONTINUOUS_CLOCK_SYNC_ONLY;
    hsram1.Init.PageSize = FMC_PAGE_SIZE_NONE;

    Timing.AddressSetupTime = 15;
    Timing.AddressHoldTime = 15;
    Timing.DataSetupTime = 70;
    Timing.BusTurnAroundDuration = 1;
    Timing.CLKDivision = 16;
    Timing.DataLatency = 17;
    Timing.AccessMode = FMC_ACCESS_MODE_A;

    ExtTiming.AddressSetupTime = 15;
    ExtTiming.AddressHoldTime = 15;
    ExtTiming.DataSetupTime = 15;
    ExtTiming.BusTurnAroundDuration = 1;
    ExtTiming.CLKDivision = 16;
    ExtTiming.DataLatency = 17;
    ExtTiming.AccessMode = FMC_ACCESS_MODE_A;

    if (HAL_SRAM_Init(&hsram1, &Timing, &ExtTiming) != HAL_OK)
    {
        return RT_FAIL;
    }
    return RT_OK;
}

static void lcd_write_reg(uint16_t regval)
{
    __NOP();
    LCD->LCD_REG = regval;
}

static uint16_t lcd_rd_data(void)
{
    volatile uint16_t ram;
    __NOP();
    ram = LCD->LCD_RAM;
    return ram;
}

static void lcd_write_data(uint16_t data)
{
    __NOP();
    LCD->LCD_RAM = data;
}

static void lcd_write_reg_data(uint16_t reg, uint16_t data)
{
    __NOP();
    LCD->LCD_REG = reg;
    LCD->LCD_RAM = data;
}

static void lcd_config_init(void)
{
    uint8_t id[2];

    lcd_write_reg(0xDB00);
    id[0] = lcd_rd_data();
    lcd_write_reg(0xDC00);
    id[1] = lcd_rd_data();
    g_lcd_info.lcd_id = (uint16_t)(id[0] << 8 | id[1]);
    printf("[LCD ] controller ID = 0x%04X\r\n", g_lcd_info.lcd_id);

    if (g_lcd_info.lcd_id == 0x8000)
    {
        /* ---------- NT35510 (正点原子) ---------- */
        lcd_write_reg_data(0xF000, 0x55);
        lcd_write_reg_data(0xF001, 0xAA);
        lcd_write_reg_data(0xF002, 0x52);
        lcd_write_reg_data(0xF003, 0x08);
        lcd_write_reg_data(0xF004, 0x01);

        lcd_write_reg_data(0xB000, 0x0D);
        lcd_write_reg_data(0xB001, 0x0D);
        lcd_write_reg_data(0xB002, 0x0D);
        lcd_write_reg_data(0xB600, 0x34);
        lcd_write_reg_data(0xB601, 0x34);
        lcd_write_reg_data(0xB602, 0x34);
        lcd_write_reg_data(0xB100, 0x0D);
        lcd_write_reg_data(0xB101, 0x0D);
        lcd_write_reg_data(0xB102, 0x0D);
        lcd_write_reg_data(0xB700, 0x34);
        lcd_write_reg_data(0xB701, 0x34);
        lcd_write_reg_data(0xB702, 0x34);
        lcd_write_reg_data(0xB200, 0x00);
        lcd_write_reg_data(0xB201, 0x00);
        lcd_write_reg_data(0xB202, 0x00);
        lcd_write_reg_data(0xB800, 0x24);
        lcd_write_reg_data(0xB801, 0x24);
        lcd_write_reg_data(0xB802, 0x24);
        lcd_write_reg_data(0xBF00, 0x01);
        lcd_write_reg_data(0xB300, 0x0F);
        lcd_write_reg_data(0xB301, 0x0F);
        lcd_write_reg_data(0xB302, 0x0F);
        lcd_write_reg_data(0xB900, 0x34);
        lcd_write_reg_data(0xB901, 0x34);
        lcd_write_reg_data(0xB902, 0x34);
        lcd_write_reg_data(0xB500, 0x08);
        lcd_write_reg_data(0xB501, 0x08);
        lcd_write_reg_data(0xB502, 0x08);
        lcd_write_reg_data(0xC200, 0x03);
        lcd_write_reg_data(0xBA00, 0x24);
        lcd_write_reg_data(0xBA01, 0x24);
        lcd_write_reg_data(0xBA02, 0x24);
        lcd_write_reg_data(0xBC00, 0x00);
        lcd_write_reg_data(0xBC01, 0x78);
        lcd_write_reg_data(0xBC02, 0x00);
        lcd_write_reg_data(0xBD00, 0x00);
        lcd_write_reg_data(0xBD01, 0x78);
        lcd_write_reg_data(0xBD02, 0x00);
        lcd_write_reg_data(0xBE00, 0x00);
        lcd_write_reg_data(0xBE01, 0x64);
        /* Gamma (R/G/B positive + negative, three pages) */
        lcd_write_reg_data(0xD100, 0x00);
        lcd_write_reg_data(0xD101, 0x33);
        lcd_write_reg_data(0xD102, 0x00);
        lcd_write_reg_data(0xD103, 0x34);
        lcd_write_reg_data(0xD104, 0x00);
        lcd_write_reg_data(0xD105, 0x3A);
        lcd_write_reg_data(0xD106, 0x00);
        lcd_write_reg_data(0xD107, 0x4A);
        lcd_write_reg_data(0xD108, 0x00);
        lcd_write_reg_data(0xD109, 0x5C);
        lcd_write_reg_data(0xD10A, 0x00);
        lcd_write_reg_data(0xD10B, 0x81);
        lcd_write_reg_data(0xD10C, 0x00);
        lcd_write_reg_data(0xD10D, 0xA6);
        lcd_write_reg_data(0xD10E, 0x00);
        lcd_write_reg_data(0xD10F, 0xE5);
        lcd_write_reg_data(0xD110, 0x01);
        lcd_write_reg_data(0xD111, 0x13);
        lcd_write_reg_data(0xD112, 0x01);
        lcd_write_reg_data(0xD113, 0x54);
        lcd_write_reg_data(0xD114, 0x01);
        lcd_write_reg_data(0xD115, 0x82);
        lcd_write_reg_data(0xD116, 0x01);
        lcd_write_reg_data(0xD117, 0xCA);
        lcd_write_reg_data(0xD118, 0x02);
        lcd_write_reg_data(0xD119, 0x00);
        lcd_write_reg_data(0xD11A, 0x02);
        lcd_write_reg_data(0xD11B, 0x01);
        lcd_write_reg_data(0xD11C, 0x02);
        lcd_write_reg_data(0xD11D, 0x34);
        lcd_write_reg_data(0xD11E, 0x02);
        lcd_write_reg_data(0xD11F, 0x67);
        lcd_write_reg_data(0xD120, 0x02);
        lcd_write_reg_data(0xD121, 0x84);
        lcd_write_reg_data(0xD122, 0x02);
        lcd_write_reg_data(0xD123, 0xA4);
        lcd_write_reg_data(0xD124, 0x02);
        lcd_write_reg_data(0xD125, 0xB7);
        lcd_write_reg_data(0xD126, 0x02);
        lcd_write_reg_data(0xD127, 0xCF);
        lcd_write_reg_data(0xD128, 0x02);
        lcd_write_reg_data(0xD129, 0xDE);
        lcd_write_reg_data(0xD12A, 0x02);
        lcd_write_reg_data(0xD12B, 0xF2);
        lcd_write_reg_data(0xD12C, 0x02);
        lcd_write_reg_data(0xD12D, 0xFE);
        lcd_write_reg_data(0xD12E, 0x03);
        lcd_write_reg_data(0xD12F, 0x10);
        lcd_write_reg_data(0xD130, 0x03);
        lcd_write_reg_data(0xD131, 0x33);
        lcd_write_reg_data(0xD132, 0x03);
        lcd_write_reg_data(0xD133, 0x6D);
        lcd_write_reg_data(0xD200, 0x00);
        lcd_write_reg_data(0xD201, 0x33);
        lcd_write_reg_data(0xD202, 0x00);
        lcd_write_reg_data(0xD203, 0x34);
        lcd_write_reg_data(0xD204, 0x00);
        lcd_write_reg_data(0xD205, 0x3A);
        lcd_write_reg_data(0xD206, 0x00);
        lcd_write_reg_data(0xD207, 0x4A);
        lcd_write_reg_data(0xD208, 0x00);
        lcd_write_reg_data(0xD209, 0x5C);
        lcd_write_reg_data(0xD20A, 0x00);
        lcd_write_reg_data(0xD20B, 0x81);
        lcd_write_reg_data(0xD20C, 0x00);
        lcd_write_reg_data(0xD20D, 0xA6);
        lcd_write_reg_data(0xD20E, 0x00);
        lcd_write_reg_data(0xD20F, 0xE5);
        lcd_write_reg_data(0xD210, 0x01);
        lcd_write_reg_data(0xD211, 0x13);
        lcd_write_reg_data(0xD212, 0x01);
        lcd_write_reg_data(0xD213, 0x54);
        lcd_write_reg_data(0xD214, 0x01);
        lcd_write_reg_data(0xD215, 0x82);
        lcd_write_reg_data(0xD216, 0x01);
        lcd_write_reg_data(0xD217, 0xCA);
        lcd_write_reg_data(0xD218, 0x02);
        lcd_write_reg_data(0xD219, 0x00);
        lcd_write_reg_data(0xD21A, 0x02);
        lcd_write_reg_data(0xD21B, 0x01);
        lcd_write_reg_data(0xD21C, 0x02);
        lcd_write_reg_data(0xD21D, 0x34);
        lcd_write_reg_data(0xD21E, 0x02);
        lcd_write_reg_data(0xD21F, 0x67);
        lcd_write_reg_data(0xD220, 0x02);
        lcd_write_reg_data(0xD221, 0x84);
        lcd_write_reg_data(0xD222, 0x02);
        lcd_write_reg_data(0xD223, 0xA4);
        lcd_write_reg_data(0xD224, 0x02);
        lcd_write_reg_data(0xD225, 0xB7);
        lcd_write_reg_data(0xD226, 0x02);
        lcd_write_reg_data(0xD227, 0xCF);
        lcd_write_reg_data(0xD228, 0x02);
        lcd_write_reg_data(0xD229, 0xDE);
        lcd_write_reg_data(0xD22A, 0x02);
        lcd_write_reg_data(0xD22B, 0xF2);
        lcd_write_reg_data(0xD22C, 0x02);
        lcd_write_reg_data(0xD22D, 0xFE);
        lcd_write_reg_data(0xD22E, 0x03);
        lcd_write_reg_data(0xD22F, 0x10);
        lcd_write_reg_data(0xD230, 0x03);
        lcd_write_reg_data(0xD231, 0x33);
        lcd_write_reg_data(0xD232, 0x03);
        lcd_write_reg_data(0xD233, 0x6D);
        lcd_write_reg_data(0xD300, 0x00);
        lcd_write_reg_data(0xD301, 0x33);
        lcd_write_reg_data(0xD302, 0x00);
        lcd_write_reg_data(0xD303, 0x34);
        lcd_write_reg_data(0xD304, 0x00);
        lcd_write_reg_data(0xD305, 0x3A);
        lcd_write_reg_data(0xD306, 0x00);
        lcd_write_reg_data(0xD307, 0x4A);
        lcd_write_reg_data(0xD308, 0x00);
        lcd_write_reg_data(0xD309, 0x5C);
        lcd_write_reg_data(0xD30A, 0x00);
        lcd_write_reg_data(0xD30B, 0x81);
        lcd_write_reg_data(0xD30C, 0x00);
        lcd_write_reg_data(0xD30D, 0xA6);
        lcd_write_reg_data(0xD30E, 0x00);
        lcd_write_reg_data(0xD30F, 0xE5);
        lcd_write_reg_data(0xD310, 0x01);
        lcd_write_reg_data(0xD311, 0x13);
        lcd_write_reg_data(0xD312, 0x01);
        lcd_write_reg_data(0xD313, 0x54);
        lcd_write_reg_data(0xD314, 0x01);
        lcd_write_reg_data(0xD315, 0x82);
        lcd_write_reg_data(0xD316, 0x01);
        lcd_write_reg_data(0xD317, 0xCA);
        lcd_write_reg_data(0xD318, 0x02);
        lcd_write_reg_data(0xD319, 0x00);
        lcd_write_reg_data(0xD31A, 0x02);
        lcd_write_reg_data(0xD31B, 0x01);
        lcd_write_reg_data(0xD31C, 0x02);
        lcd_write_reg_data(0xD31D, 0x34);
        lcd_write_reg_data(0xD31E, 0x02);
        lcd_write_reg_data(0xD31F, 0x67);
        lcd_write_reg_data(0xD320, 0x02);
        lcd_write_reg_data(0xD321, 0x84);
        lcd_write_reg_data(0xD322, 0x02);
        lcd_write_reg_data(0xD323, 0xA4);
        lcd_write_reg_data(0xD324, 0x02);
        lcd_write_reg_data(0xD325, 0xB7);
        lcd_write_reg_data(0xD326, 0x02);
        lcd_write_reg_data(0xD327, 0xCF);
        lcd_write_reg_data(0xD328, 0x02);
        lcd_write_reg_data(0xD329, 0xDE);
        lcd_write_reg_data(0xD32A, 0x02);
        lcd_write_reg_data(0xD32B, 0xF2);
        lcd_write_reg_data(0xD32C, 0x02);
        lcd_write_reg_data(0xD32D, 0xFE);
        lcd_write_reg_data(0xD32E, 0x03);
        lcd_write_reg_data(0xD32F, 0x10);
        lcd_write_reg_data(0xD330, 0x03);
        lcd_write_reg_data(0xD331, 0x33);
        lcd_write_reg_data(0xD332, 0x03);
        lcd_write_reg_data(0xD333, 0x6D);
        lcd_write_reg_data(0xD400, 0x00);
        lcd_write_reg_data(0xD401, 0x33);
        lcd_write_reg_data(0xD402, 0x00);
        lcd_write_reg_data(0xD403, 0x34);
        lcd_write_reg_data(0xD404, 0x00);
        lcd_write_reg_data(0xD405, 0x3A);
        lcd_write_reg_data(0xD406, 0x00);
        lcd_write_reg_data(0xD407, 0x4A);
        lcd_write_reg_data(0xD408, 0x00);
        lcd_write_reg_data(0xD409, 0x5C);
        lcd_write_reg_data(0xD40A, 0x00);
        lcd_write_reg_data(0xD40B, 0x81);
        lcd_write_reg_data(0xD40C, 0x00);
        lcd_write_reg_data(0xD40D, 0xA6);
        lcd_write_reg_data(0xD40E, 0x00);
        lcd_write_reg_data(0xD40F, 0xE5);
        lcd_write_reg_data(0xD410, 0x01);
        lcd_write_reg_data(0xD411, 0x13);
        lcd_write_reg_data(0xD412, 0x01);
        lcd_write_reg_data(0xD413, 0x54);
        lcd_write_reg_data(0xD414, 0x01);
        lcd_write_reg_data(0xD415, 0x82);
        lcd_write_reg_data(0xD416, 0x01);
        lcd_write_reg_data(0xD417, 0xCA);
        lcd_write_reg_data(0xD418, 0x02);
        lcd_write_reg_data(0xD419, 0x00);
        lcd_write_reg_data(0xD41A, 0x02);
        lcd_write_reg_data(0xD41B, 0x01);
        lcd_write_reg_data(0xD41C, 0x02);
        lcd_write_reg_data(0xD41D, 0x34);
        lcd_write_reg_data(0xD41E, 0x02);
        lcd_write_reg_data(0xD41F, 0x67);
        lcd_write_reg_data(0xD420, 0x02);
        lcd_write_reg_data(0xD421, 0x84);
        lcd_write_reg_data(0xD422, 0x02);
        lcd_write_reg_data(0xD423, 0xA4);
        lcd_write_reg_data(0xD424, 0x02);
        lcd_write_reg_data(0xD425, 0xB7);
        lcd_write_reg_data(0xD426, 0x02);
        lcd_write_reg_data(0xD427, 0xCF);
        lcd_write_reg_data(0xD428, 0x02);
        lcd_write_reg_data(0xD429, 0xDE);
        lcd_write_reg_data(0xD42A, 0x02);
        lcd_write_reg_data(0xD42B, 0xF2);
        lcd_write_reg_data(0xD42C, 0x02);
        lcd_write_reg_data(0xD42D, 0xFE);
        lcd_write_reg_data(0xD42E, 0x03);
        lcd_write_reg_data(0xD42F, 0x10);
        lcd_write_reg_data(0xD430, 0x03);
        lcd_write_reg_data(0xD431, 0x33);
        lcd_write_reg_data(0xD432, 0x03);
        lcd_write_reg_data(0xD433, 0x6D);
        lcd_write_reg_data(0xD500, 0x00);
        lcd_write_reg_data(0xD501, 0x33);
        lcd_write_reg_data(0xD502, 0x00);
        lcd_write_reg_data(0xD503, 0x34);
        lcd_write_reg_data(0xD504, 0x00);
        lcd_write_reg_data(0xD505, 0x3A);
        lcd_write_reg_data(0xD506, 0x00);
        lcd_write_reg_data(0xD507, 0x4A);
        lcd_write_reg_data(0xD508, 0x00);
        lcd_write_reg_data(0xD509, 0x5C);
        lcd_write_reg_data(0xD50A, 0x00);
        lcd_write_reg_data(0xD50B, 0x81);
        lcd_write_reg_data(0xD50C, 0x00);
        lcd_write_reg_data(0xD50D, 0xA6);
        lcd_write_reg_data(0xD50E, 0x00);
        lcd_write_reg_data(0xD50F, 0xE5);
        lcd_write_reg_data(0xD510, 0x01);
        lcd_write_reg_data(0xD511, 0x13);
        lcd_write_reg_data(0xD512, 0x01);
        lcd_write_reg_data(0xD513, 0x54);
        lcd_write_reg_data(0xD514, 0x01);
        lcd_write_reg_data(0xD515, 0x82);
        lcd_write_reg_data(0xD516, 0x01);
        lcd_write_reg_data(0xD517, 0xCA);
        lcd_write_reg_data(0xD518, 0x02);
        lcd_write_reg_data(0xD519, 0x00);
        lcd_write_reg_data(0xD51A, 0x02);
        lcd_write_reg_data(0xD51B, 0x01);
        lcd_write_reg_data(0xD51C, 0x02);
        lcd_write_reg_data(0xD51D, 0x34);
        lcd_write_reg_data(0xD51E, 0x02);
        lcd_write_reg_data(0xD51F, 0x67);
        lcd_write_reg_data(0xD520, 0x02);
        lcd_write_reg_data(0xD521, 0x84);
        lcd_write_reg_data(0xD522, 0x02);
        lcd_write_reg_data(0xD523, 0xA4);
        lcd_write_reg_data(0xD524, 0x02);
        lcd_write_reg_data(0xD525, 0xB7);
        lcd_write_reg_data(0xD526, 0x02);
        lcd_write_reg_data(0xD527, 0xCF);
        lcd_write_reg_data(0xD528, 0x02);
        lcd_write_reg_data(0xD529, 0xDE);
        lcd_write_reg_data(0xD52A, 0x02);
        lcd_write_reg_data(0xD52B, 0xF2);
        lcd_write_reg_data(0xD52C, 0x02);
        lcd_write_reg_data(0xD52D, 0xFE);
        lcd_write_reg_data(0xD52E, 0x03);
        lcd_write_reg_data(0xD52F, 0x10);
        lcd_write_reg_data(0xD530, 0x03);
        lcd_write_reg_data(0xD531, 0x33);
        lcd_write_reg_data(0xD532, 0x03);
        lcd_write_reg_data(0xD533, 0x6D);
        lcd_write_reg_data(0xD600, 0x00);
        lcd_write_reg_data(0xD601, 0x33);
        lcd_write_reg_data(0xD602, 0x00);
        lcd_write_reg_data(0xD603, 0x34);
        lcd_write_reg_data(0xD604, 0x00);
        lcd_write_reg_data(0xD605, 0x3A);
        lcd_write_reg_data(0xD606, 0x00);
        lcd_write_reg_data(0xD607, 0x4A);
        lcd_write_reg_data(0xD608, 0x00);
        lcd_write_reg_data(0xD609, 0x5C);
        lcd_write_reg_data(0xD60A, 0x00);
        lcd_write_reg_data(0xD60B, 0x81);
        lcd_write_reg_data(0xD60C, 0x00);
        lcd_write_reg_data(0xD60D, 0xA6);
        lcd_write_reg_data(0xD60E, 0x00);
        lcd_write_reg_data(0xD60F, 0xE5);
        lcd_write_reg_data(0xD610, 0x01);
        lcd_write_reg_data(0xD611, 0x13);
        lcd_write_reg_data(0xD612, 0x01);
        lcd_write_reg_data(0xD613, 0x54);
        lcd_write_reg_data(0xD614, 0x01);
        lcd_write_reg_data(0xD615, 0x82);
        lcd_write_reg_data(0xD616, 0x01);
        lcd_write_reg_data(0xD617, 0xCA);
        lcd_write_reg_data(0xD618, 0x02);
        lcd_write_reg_data(0xD619, 0x00);
        lcd_write_reg_data(0xD61A, 0x02);
        lcd_write_reg_data(0xD61B, 0x01);
        lcd_write_reg_data(0xD61C, 0x02);
        lcd_write_reg_data(0xD61D, 0x34);
        lcd_write_reg_data(0xD61E, 0x02);
        lcd_write_reg_data(0xD61F, 0x67);
        lcd_write_reg_data(0xD620, 0x02);
        lcd_write_reg_data(0xD621, 0x84);
        lcd_write_reg_data(0xD622, 0x02);
        lcd_write_reg_data(0xD623, 0xA4);
        lcd_write_reg_data(0xD624, 0x02);
        lcd_write_reg_data(0xD625, 0xB7);
        lcd_write_reg_data(0xD626, 0x02);
        lcd_write_reg_data(0xD627, 0xCF);
        lcd_write_reg_data(0xD628, 0x02);
        lcd_write_reg_data(0xD629, 0xDE);
        lcd_write_reg_data(0xD62A, 0x02);
        lcd_write_reg_data(0xD62B, 0xF2);
        lcd_write_reg_data(0xD62C, 0x02);
        lcd_write_reg_data(0xD62D, 0xFE);
        lcd_write_reg_data(0xD62E, 0x03);
        lcd_write_reg_data(0xD62F, 0x10);
        lcd_write_reg_data(0xD630, 0x03);
        lcd_write_reg_data(0xD631, 0x33);
        lcd_write_reg_data(0xD632, 0x03);
        lcd_write_reg_data(0xD633, 0x6D);
        /* LV2 Page 0 enable */
        lcd_write_reg_data(0xF000, 0x55);
        lcd_write_reg_data(0xF001, 0xAA);
        lcd_write_reg_data(0xF002, 0x52);
        lcd_write_reg_data(0xF003, 0x08);
        lcd_write_reg_data(0xF004, 0x00);
        lcd_write_reg_data(0xB100, 0xCC);
        lcd_write_reg_data(0xB101, 0x00);
        lcd_write_reg_data(0xB600, 0x05);
        lcd_write_reg_data(0xB700, 0x70);
        lcd_write_reg_data(0xB701, 0x70);
        lcd_write_reg_data(0xB800, 0x01);
        lcd_write_reg_data(0xB801, 0x03);
        lcd_write_reg_data(0xB802, 0x03);
        lcd_write_reg_data(0xB803, 0x03);
        lcd_write_reg_data(0xBC00, 0x02);
        lcd_write_reg_data(0xBC01, 0x00);
        lcd_write_reg_data(0xBC02, 0x00);
        lcd_write_reg_data(0xC900, 0xD0);
        lcd_write_reg_data(0xC901, 0x02);
        lcd_write_reg_data(0xC902, 0x50);
        lcd_write_reg_data(0xC903, 0x50);
        lcd_write_reg_data(0xC904, 0x50);
        lcd_write_reg_data(0x3500, 0x00);
        lcd_write_reg_data(0x3A00, 0x55);  /* 16-bit/pixel */
        lcd_write_reg(0x1100);
        lcd_delay_us(120);
        lcd_write_reg(0x2900);
    }
    else
    {
        /* ---------- ILI9806E (正点原子) generic MIPI-DCS bring-up ----------
         * The NT35510 table above does not match this controller.  This is a
         * minimal standard init that should light the panel; tune the gamma /
         * VCOM registers on real hardware.  Orientation is applied by
         * lcd_scan_dir() below. */
        printf("[LCD ] using ILI9806E generic init (tune on hardware)\r\n");
        lcd_write_reg(0x01);          /* software reset */
        lcd_delay_us(10);
        lcd_delay_us(120);
        lcd_write_reg(0x11);          /* sleep out */
        lcd_delay_us(120);
        lcd_write_reg_data(0x3A, 0x55);   /* 16 bpp RGB565 */
        lcd_write_reg_data(0x36, 0x00);   /* MADCTL (tune for orientation) */
        lcd_write_reg(0x29);          /* display on */
        lcd_delay_us(20);
    }

    /* Tighten the write timing for the NE1 bank (write burst off). */
    FMC_Bank1E->BWTR[0] &= ~(0xFU << 0);
    FMC_Bank1E->BWTR[0] &= ~(0xFU << 8);
    FMC_Bank1E->BWTR[0] |= (4U << 0);
    FMC_Bank1E->BWTR[0] |= (4U << 8);

    lcd_display_dir(0);
    lcd_driver_clear(LCD_BLACK);
    LCD_BACKLIGHT_ON();
    g_lcd_ready = 1U;
    printf("[LCD ] active GRAM window %lux%lu (panel spec 800x480)\r\n",
           (unsigned long)g_lcd_info.lcd_width, (unsigned long)g_lcd_info.lcd_height);
}

static void lcd_display_dir(uint8_t dir)
{
    (void)dir;
    /* Width/height are fixed by the panel; just pick the scan direction. */
    lcd_scan_dir(DFT_SCAN_DIR);
}

static void lcd_scan_dir(uint8_t dir)
{
    uint16_t regval = 0;
    uint16_t dirreg = 0;
    uint16_t temp;

    switch (dir)
    {
        case L2R_U2D:  regval |= (0 << 7) | (0 << 6) | (0 << 5); break;
        case L2R_D2U:  regval |= (1 << 7) | (0 << 6) | (0 << 5); break;
        case R2L_U2D:  regval |= (0 << 7) | (1 << 6) | (0 << 5); break;
        case R2L_D2U:  regval |= (1 << 7) | (1 << 6) | (0 << 5); break;
        case U2D_L2R:  regval |= (0 << 7) | (0 << 6) | (1 << 5); break;
        case U2D_R2L:  regval |= (0 << 7) | (1 << 6) | (1 << 5); break;
        case D2U_L2R:  regval |= (1 << 7) | (0 << 6) | (1 << 5); break;
        case D2U_R2L:  regval |= (1 << 7) | (1 << 6) | (1 << 5); break;
    }

    dirreg = 0x3600;
    lcd_write_reg_data(dirreg, regval);
    if (regval & 0x20)
    {
        if (g_lcd_info.lcd_width < g_lcd_info.lcd_height)
        {
            temp = (uint16_t)g_lcd_info.lcd_width;
            g_lcd_info.lcd_width = g_lcd_info.lcd_height;
            g_lcd_info.lcd_height = temp;
        }
    }
    else
    {
        if (g_lcd_info.lcd_width > g_lcd_info.lcd_height)
        {
            temp = (uint16_t)g_lcd_info.lcd_width;
            g_lcd_info.lcd_width = g_lcd_info.lcd_height;
            g_lcd_info.lcd_height = temp;
        }
    }

    /* define the screen region */
    lcd_write_reg(g_lcd_info.setxcmd);
    lcd_write_data(0);
    lcd_write_reg(g_lcd_info.setxcmd + 1);
    lcd_write_data(0);
    lcd_write_reg(g_lcd_info.setxcmd + 2);
    lcd_write_data((uint16_t)((g_lcd_info.lcd_width - 1) >> 8));
    lcd_write_reg(g_lcd_info.setxcmd + 3);
    lcd_write_data((uint16_t)((g_lcd_info.lcd_width - 1) & 0xFF));
    lcd_write_reg(g_lcd_info.setycmd);
    lcd_write_data(0);
    lcd_write_reg(g_lcd_info.setycmd + 1);
    lcd_write_data(0);
    lcd_write_reg(g_lcd_info.setycmd + 2);
    lcd_write_data((uint16_t)((g_lcd_info.lcd_height - 1) >> 8));
    lcd_write_reg(g_lcd_info.setycmd + 3);
    lcd_write_data((uint16_t)((g_lcd_info.lcd_height - 1) & 0xFF));
}
