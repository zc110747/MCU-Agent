/**
  ******************************************************************************
  * @file    bsp_sdram.c
  * @brief   External SDRAM driver: FMC Bank1 + W9825G6KH-6 (32 MB).
  *
  *          Pin map (see document/stm32f4_hw.md):
  *            Data:   PD0,1,8,9,10,14,15 / PE7..15
  *            Addr:   PF0..5, PF12..15, PG0,1,2
  *            Bank:   PG4 (BA0), PG5 (BA1)
  *            Ctrl:   PC0 (SDNWE), PG15 (SDNCAS), PF11 (SDNRAS),
  *                    PC2 (SDNE0), PC3 (SDCKE0), PG8 (SDCLK)
  *            Mask:   PE0 (NBL0), PE1 (NBL1)
  *
  *          FMC clock = 180/2 = 90 MHz.  Self-test uses direct pointer
  *          access (no section tricks) so it works before the linker
  *          script SDRAM sections are used.
  ******************************************************************************
  */
#include "bsp_sdram.h"
#include "bsp_delay.h"
#include "stm32f4xx_hal.h"

SDRAM_HandleTypeDef hsdram1;

/* ---- low-level helpers ---- */
static int sdram_send_command(uint8_t bank, uint8_t cmd, uint8_t refresh, uint16_t regval)
{
  FMC_SDRAM_CommandTypeDef Command = {0};

  Command.CommandMode = cmd;
  Command.CommandTarget = (bank == 0) ? FMC_SDRAM_CMD_TARGET_BANK1
                                      : FMC_SDRAM_CMD_TARGET_BANK2;
  Command.AutoRefreshNumber = refresh;
  Command.ModeRegisterDefinition = regval;

  return (HAL_SDRAM_SendCommand(&hsdram1, &Command, 0x1000) == HAL_OK) ? 0 : -1;
}

static int sdram_hardware_init(void)
{
  FMC_SDRAM_TimingTypeDef SdramTiming = {0};
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Enable clocks */
  __HAL_RCC_FMC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* FMC AF12 pins, very high speed */
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  /* PF0..5 (A0..A5), PF12..15 (A6..A9), PF11 (SDNRAS) */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
                        GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /* PC0 (SDNWE), PC2 (SDNE0), PC3 (SDCKE0) */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* PG0..2 (A10..A12), PG4/5 (BA0/BA1), PG8 (SDCLK), PG15 (SDNCAS) */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
                        GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* PE0/1 (NBL0/NBL1), PE7..15 (D4..D12) */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
                        GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                        GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* PD0/1 (D2/D3), PD8/9/10 (D13/14/15), PD14/15 (D0/D1) */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
                        GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* FMC SDRAM controller: W9825G6KH, Bank1, 16-bit, 13 rows x 9 cols x 4 banks */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;   /* 90 MHz */
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

  /* Timing @90 MHz (11.1 ns/tck), W9825G6KH datasheet */
  SdramTiming.LoadToActiveDelay = 2;    /* tRSC >= 20 ns */
  SdramTiming.ExitSelfRefreshDelay = 8; /* tXSR >= 75 ns */
  SdramTiming.SelfRefreshTime = 6;      /* tRAS 45..100000 ns */
  SdramTiming.RowCycleDelay = 6;        /* tRC  >= 65 ns */
  SdramTiming.WriteRecoveryTime = 2;    /* tWR  >= 1 clk + tRAS */
  SdramTiming.RPDelay = 2;              /* tRP  >= 20 ns */
  SdramTiming.RCDDelay = 2;             /* tRCD >= 20 ns */

  return (HAL_SDRAM_Init(&hsdram1, &SdramTiming) == HAL_OK) ? 0 : -1;
}

static int sdram_initialize_sequence(void)
{
  uint32_t temp;
  int result = 0;

  result |= sdram_send_command(0, FMC_SDRAM_CMD_CLK_ENABLE, 1, 0);
  bsp_delay_us(1000U);   /* >= 100 us: settle before PALL (DWT busy-wait) */
  result |= sdram_send_command(0, FMC_SDRAM_CMD_PALL, 1, 0);
  result |= sdram_send_command(0, FMC_SDRAM_CMD_AUTOREFRESH_MODE, 1, 0);

  /* Mode register: BL=1, sequential, CAS=3, standard op, single write burst */
  temp = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1
       | SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL
       | SDRAM_MODEREG_CAS_LATENCY_3
       | SDRAM_MODEREG_OPERATING_MODE_STANDARD
       | SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
  result |= sdram_send_command(0, FMC_SDRAM_CMD_LOAD_MODE, 1, (uint16_t)temp);

  return result;
}

/* Direct pointer self-test: 3 regions x 4 patterns (data + address lines). */
static int sdram_memory_test(void)
{
  static const uint32_t regions[] = {
    0x00000000u,                    /* start */
    0x00800000u,                    /* middle */
    SDRAM_SIZE - 0x100u             /* end */
  };
  const uint32_t patterns[] = { 0xAA55AA55u, 0x55AA55AAu, 0xDEADBEEFu, 0x12345678u };
  volatile uint32_t *base = (volatile uint32_t *)SDRAM_BASE;
  int i, r, p;

  for (r = 0; r < 3; r++)
  {
    volatile uint32_t *p32 = base + (regions[r] / 4u);
    for (i = 0; i < 64; i++)
    {
      p32[i] = 0x00000000u;
    }
    for (p = 0; p < 4; p++)
    {
      for (i = 0; i < 64; i++)
      {
        p32[i] = patterns[p];
      }
      for (i = 0; i < 64; i++)
      {
        if (p32[i] != patterns[p])
          {
            return -1;
          }
      }
    }
  }
  return 0;
}

int bsp_sdram_init(void)
{
  if (sdram_hardware_init() != 0)
  {
    return -1;
  }
  if (sdram_initialize_sequence() != 0)
  {
    return -1;
  }
  /* Refresh counter: 64ms / 11.1ns / 8192 rows - 20 = 683 */
  HAL_SDRAM_ProgramRefreshRate(&hsdram1, 683);

  if (sdram_memory_test() != 0)
  {
    return -1;
  }
  return 0;
}
