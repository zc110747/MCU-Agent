/**
 * @file    stm32h7xx_hal_msp.c
 * @brief   Low level peripheral init: DCMI (+DMA2_Stream7), I2C4 (SCCB),
 *          SPI6 (ST7789 OLED), USART1 (debug console).
 */
#include "main.h"

extern DMA_HandleTypeDef hdma_dcmi;

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
}

/* ------------------------------------------------------------------- DCMI */
void HAL_DCMI_MspInit(DCMI_HandleTypeDef *hdcmi)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (hdcmi->Instance != DCMI)
    {
        return;
    }

    __HAL_RCC_DCMI_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* PE4 D4, PE5 D6, PE6 D7 | PA4 HSYNC, PA6 PIXCLK | PC6 D0, PC7 D1
     * PD3 D5 | PG9 VSYNC, PG10 D2, PG11 D3                                 */
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;

    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* DMA2_Stream7 in double-buffer mode is armed later by drv_dcmi.c; here we
     * only provide the base configuration and the handle link.              */
    hdma_dcmi.Instance                 = DMA2_Stream7;
    hdma_dcmi.Init.Request             = DMA_REQUEST_DCMI;
    hdma_dcmi.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_dcmi.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_dcmi.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_dcmi.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    hdma_dcmi.Init.Mode                = DMA_CIRCULAR;
    hdma_dcmi.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_dcmi.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
    hdma_dcmi.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    hdma_dcmi.Init.MemBurst            = DMA_MBURST_SINGLE;
    hdma_dcmi.Init.PeriphBurst         = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&hdma_dcmi) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_LINKDMA(hdcmi, DMA_Handle, hdma_dcmi);

    HAL_NVIC_SetPriority(DCMI_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DCMI_IRQn);
}

void HAL_DCMI_MspDeInit(DCMI_HandleTypeDef *hdcmi)
{
    if (hdcmi->Instance != DCMI)
    {
        return;
    }
    __HAL_RCC_DCMI_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4 | GPIO_PIN_6);
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_6 | GPIO_PIN_7);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_3);
    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11);
    HAL_DMA_DeInit(hdcmi->DMA_Handle);
    HAL_NVIC_DisableIRQ(DCMI_IRQn);
}

/* ------------------------------------------------------------------- I2C4 */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef         GPIO_InitStruct     = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    if (hi2c->Instance != I2C4)
    {
        return;
    }

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
    PeriphClkInitStruct.I2c4ClockSelection   = RCC_I2C4CLKSOURCE_D3PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* PF14 SCL, PF15 SDA */
    GPIO_InitStruct.Pin       = SCCB_SCL_Pin | SCCB_SDA_Pin;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C4;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    __HAL_RCC_I2C4_CLK_ENABLE();
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C4)
    {
        return;
    }
    __HAL_RCC_I2C4_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOF, SCCB_SCL_Pin | SCCB_SDA_Pin);
}

/* ------------------------------------------------------------------- SPI6 */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef         GPIO_InitStruct     = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    if (hspi->Instance != SPI6)
    {
        return;
    }

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI6;
    PeriphClkInitStruct.Spi6ClockSelection   = RCC_SPI6CLKSOURCE_D3PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_RCC_SPI6_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* PG8 NSS, PG13 SCK, PG14 MOSI */
    GPIO_InitStruct.Pin       = GPIO_PIN_8 | GPIO_PIN_13 | GPIO_PIN_14;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI6;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI6)
    {
        return;
    }
    __HAL_RCC_SPI6_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_8 | GPIO_PIN_13 | GPIO_PIN_14);
}

/* ----------------------------------------------------------------- USART1 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef         GPIO_InitStruct     = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    if (huart->Instance != USART1)
    {
        return;
    }

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA9 TX, PA10 RX */
    GPIO_InitStruct.Pin       = DBG_UART_TX_Pin | DBG_UART_RX_Pin;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(DBG_UART_GPIO_Port, &GPIO_InitStruct);
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
    {
        return;
    }
    __HAL_RCC_USART1_CLK_DISABLE();
    HAL_GPIO_DeInit(DBG_UART_GPIO_Port, DBG_UART_TX_Pin | DBG_UART_RX_Pin);
}
