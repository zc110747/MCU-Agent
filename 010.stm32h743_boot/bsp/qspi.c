/**
  ******************************************************************************
  * @file    bsp/qspi.c
  * @brief   QSPI (QUADSPI) driver for Winbond W25Q64JV Flash on STM32H743
  ******************************************************************************
  */
#include "qspi.h"
#include "uart.h"

QSPI_HandleTypeDef hqspi;
static uint8_t qspi_inited = 0;

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                           */
/* -------------------------------------------------------------------------- */

static void QSPI_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}

static QSPI_Status_t QSPI_WaitNotBusy(uint32_t timeout)
{
    QSPI_CommandTypeDef cmd = {0};
    uint8_t status;
    uint32_t tickstart = HAL_GetTick();

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_READ_STATUS_REG1;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = 1;

    do {
        if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
            return QSPI_ERR_TIMEOUT;
        if (HAL_QSPI_Receive(&hqspi, &status, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
            return QSPI_ERR_TIMEOUT;

        if ((status & W25X_SR1_BUSY) == 0)
            return QSPI_OK;

        if ((HAL_GetTick() - tickstart) > timeout)
            return QSPI_ERR_TIMEOUT;
    } while (1);
}

static QSPI_Status_t QSPI_WriteEnable(void)
{
    QSPI_CommandTypeDef cmd = {0};

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_WRITE_ENABLE;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_WRITE;
    return QSPI_OK;
}

static QSPI_Status_t QSPI_EnableQuadMode(void);

static QSPI_Status_t QSPI_Reset(void)
{
    QSPI_CommandTypeDef cmd = {0};

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_RESET_ENABLE;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_INIT;

    cmd.Instruction = W25X_RESET_MEMORY;
    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_INIT;

    QSPI_Delay(10);
    return QSPI_OK;
}

uint8_t BSP_QSPI_IsBusy(void)
{
    QSPI_CommandTypeDef cmd = {0};
    uint8_t status = 0;

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_READ_STATUS_REG1;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = 1;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) == HAL_OK)
        HAL_QSPI_Receive(&hqspi, &status, HAL_QPSI_TIMEOUT_DEFAULT_VALUE);

    return (status & W25X_SR1_BUSY) ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

QSPI_Status_t BSP_QSPI_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Idempotent: the peripheral is configured once. Re-running the full init
       (e.g. from FatFs disk_initialize after the self-test / XIP-disable) can
       leave the QUADSPI state machine wedged, so we only do it the first time. */
    if (qspi_inited)
        return QSPI_OK;

    /* Enable clocks */
    __HAL_RCC_QSPI_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;

    /* PF6/PF7/PF10 = QUADSPI IO3/IO2/CLK  -> AF9
       PF8/PF9      = QUADSPI IO0/IO1       -> AF10   (per STM32H743 DS) */
    gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_10;
    gpio.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOF, &gpio);

    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOF, &gpio);

    /* PG6 = QUADSPI BK1_NCS (AF10) */
    gpio.Pin       = GPIO_PIN_6;
    gpio.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOG, &gpio);

    hqspi.Instance = QUADSPI;
    hqspi.Init.ClockPrescaler     = 4;     /* QSPI kernel 240MHz/4 = 60MHz (safe) */
    hqspi.Init.FifoThreshold      = 4;
    hqspi.Init.SampleShifting     = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    hqspi.Init.FlashSize          = POSITION_VAL(QSPI_FLASH_SIZE) - 1; /* 23 for 8MB */
    hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
    hqspi.Init.ClockMode          = QSPI_CLOCK_MODE_0;
    hqspi.Init.FlashID            = QSPI_FLASH_ID_1;
    hqspi.Init.DualFlash          = QSPI_DUALFLASH_DISABLE;

    if (HAL_QSPI_Init(&hqspi) != HAL_OK)
        return QSPI_ERR_INIT;

    if (QSPI_Reset() != QSPI_OK)
        return QSPI_ERR_INIT;

    if (QSPI_EnableQuadMode() != QSPI_OK)
        return QSPI_ERR_INIT;

    qspi_inited = 1;
    return QSPI_OK;
}

/* Enable the Quad Enable (QE) bit in Status Register 2 so that quad
   (4-line) commands such as 0xEB work in memory-mapped mode. */
static QSPI_Status_t QSPI_EnableQuadMode(void)
{
    QSPI_CommandTypeDef cmd = {0};
    uint8_t sr2;

    /* Read current SR2 */
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_READ_STATUS_REG2;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = 1;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_INIT;
    if (HAL_QSPI_Receive(&hqspi, &sr2, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_INIT;

    if (sr2 & W25X_SR2_QE)
        return QSPI_OK;   /* already enabled */

    if (QSPI_WriteEnable() != QSPI_OK)
        return QSPI_ERR_INIT;

    sr2 |= W25X_SR2_QE;
    cmd.Instruction = 0x31;   /* Write Status Register 2 */
    cmd.NbData      = 1;
    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_INIT;
    if (HAL_QSPI_Transmit(&hqspi, &sr2, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_INIT;

    if (QSPI_WaitNotBusy(1000) != QSPI_OK)
        return QSPI_ERR_INIT;

    return QSPI_OK;
}

QSPI_Status_t BSP_QSPI_ReadID(uint8_t *id, uint8_t len)
{
    QSPI_CommandTypeDef cmd = {0};

    if (id == NULL || len == 0)
        return QSPI_ERR_READ;

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_READ_JEDEC_ID;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = len;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_ID;

    if (HAL_QSPI_Receive(&hqspi, id, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_ID;

    return QSPI_OK;
}

QSPI_Status_t BSP_QSPI_EraseSector(uint32_t addr)
{
    QSPI_CommandTypeDef cmd = {0};
    HAL_QSPI_Abort(&hqspi);   /* flush peripheral before issuing indirect cmds */

    if (QSPI_WaitNotBusy(3000) != QSPI_OK)
        return QSPI_ERR_ERASE;
    if (QSPI_WriteEnable() != QSPI_OK)
        return QSPI_ERR_ERASE;

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_SECTOR_ERASE;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
    cmd.Address           = addr & 0x0FFFFFFF;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_ERASE;

    if (QSPI_WaitNotBusy(3000) != QSPI_OK)
        return QSPI_ERR_ERASE;

    return QSPI_OK;
}

QSPI_Status_t BSP_QSPI_WritePage(uint32_t addr, const uint8_t *data, uint16_t len)
{
    QSPI_CommandTypeDef cmd = {0};

    if (data == NULL || len == 0 || len > QSPI_PAGE_SIZE)
        return QSPI_ERR_WRITE;
    HAL_QSPI_Abort(&hqspi);   /* flush peripheral before issuing indirect cmds */
    if (QSPI_WaitNotBusy(3000) != QSPI_OK)
        return QSPI_ERR_WRITE;
    if (QSPI_WriteEnable() != QSPI_OK)
        return QSPI_ERR_WRITE;

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_QUAD_PAGE_PROGRAM;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
    cmd.Address           = addr & 0x0FFFFFFF;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_4_LINES;
    cmd.DummyCycles       = 0;
    cmd.NbData            = len;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_WRITE;
    if (HAL_QSPI_Transmit(&hqspi, (uint8_t *)data, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_WRITE;

    if (QSPI_WaitNotBusy(3000) != QSPI_OK)
        return QSPI_ERR_WRITE;

    return QSPI_OK;
}

/* Indirect (HAL) read -- single line, 0x03 Read Data command */
QSPI_Status_t BSP_QSPI_ReadIndirect(uint32_t addr, uint8_t *buf, uint32_t len)
{
    QSPI_CommandTypeDef cmd = {0};

    if (buf == NULL || len == 0)
        return QSPI_ERR_READ;

    /* Flush any lingering peripheral state from a prior memory-mapped / transfer
       so the next indirect read starts clean (avoids H7 QUADSPI stall). */
    HAL_QSPI_Abort(&hqspi);

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_FAST_READ_QUAD;
    cmd.AddressMode       = QSPI_ADDRESS_4_LINES;
    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
    cmd.Address           = addr & 0x0FFFFFFF;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_4_LINES;
    cmd.DummyCycles       = 6;        /* W25Q64JV Quad I/O read needs 6 dummy */
    cmd.NbData            = len;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_READ;
    if (HAL_QSPI_Receive(&hqspi, buf, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return QSPI_ERR_READ;

    return QSPI_OK;
}

/* Memory-mapped (XIP) mode using Quad I/O Fast Read (0xEB) */
QSPI_Status_t BSP_QSPI_EnableMemoryMapped(void)
{
    QSPI_CommandTypeDef cmd = {0};
    QSPI_MemoryMappedTypeDef mm = {0};

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25X_FAST_READ_QUAD;
    cmd.AddressMode       = QSPI_ADDRESS_4_LINES;
    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
    cmd.AlternateBytesSize= QSPI_ALTERNATE_BYTES_8_BITS;
    cmd.AlternateBytes    = 0x00;
    cmd.DummyCycles       = 4;
    cmd.DataMode          = QSPI_DATA_4_LINES;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    mm.TimeOutActivation  = QSPI_TIMEOUT_COUNTER_DISABLE;
    mm.TimeOutPeriod      = 0;

    if (HAL_QSPI_MemoryMapped(&hqspi, &cmd, &mm) != HAL_OK)
        return QSPI_ERR_READ;

    return QSPI_OK;
}

QSPI_Status_t BSP_QSPI_DisableMemoryMapped(void)
{
    /* Abort any ongoing memory-mapped access to return to indirect mode */
    if (HAL_QSPI_Abort(&hqspi) != HAL_OK)
        return QSPI_ERR_READ;
    return QSPI_OK;
}
