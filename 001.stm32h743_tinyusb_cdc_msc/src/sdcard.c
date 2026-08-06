/* ---------------------------------------------------------------------------
 * SD card driver - STM32H743 SDMMC1 (see sdcard.h for the pin map)
 * -------------------------------------------------------------------------*/

#include "sdcard.h"
#include "stm32h7xx_hal.h"

SD_HandleTypeDef hsd1;

static bool     s_ready  = false;
static uint32_t s_blocks = 0;
static uint32_t s_bsize  = 512;

/* ------------------------------------------------------------------------ */
/* HAL MSP hook - runs from inside HAL_SD_Init()                            */
/* ------------------------------------------------------------------------ */
void HAL_SD_MspInit(SD_HandleTypeDef* hsd) {
  GPIO_InitTypeDef g = {0};

  if (hsd->Instance != SDMMC1) return;

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_SDMMC1_CLK_ENABLE();

  /* SDMMC1 alternate function is AF12 on every relevant pin. */
  g.Mode      = GPIO_MODE_AF_PP;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF12_SDMMC1;

  /* Data lines D0..D3 + CMD get pull-ups; the clock line stays floating. */
  g.Pin  = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &g);

  g.Pin  = GPIO_PIN_12;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &g);

  g.Pin  = GPIO_PIN_2;            /* CMD */
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &g);
}

void HAL_SD_MspDeInit(SD_HandleTypeDef* hsd) {
  if (hsd->Instance != SDMMC1) return;
  __HAL_RCC_SDMMC1_CLK_DISABLE();
}

/* ------------------------------------------------------------------------ */
void sdcard_init(void) {
  HAL_SD_CardInfoTypeDef info;

  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  /* SDMMC_CK = PLL1Q(100 MHz) / (2 * ClockDiv) = 100 / 4 = 25 MHz.
   * HAL drops this to <=400 kHz on its own for the card-identification phase. */
  hsd1.Init.ClockDiv            = 2;

  s_ready = false;

  if (HAL_SD_Init(&hsd1) != HAL_OK)                               return;
  if (HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) != HAL_OK) return;

  if (HAL_SD_GetCardInfo(&hsd1, &info) == HAL_OK) {
    s_blocks = info.LogBlockNbr;
    s_bsize  = info.LogBlockSize;   /* 512 */
  }
  s_ready = (s_blocks > 0);
}

bool sdcard_present(void) { return s_ready; }

uint32_t sdcard_block_count(void) { return s_blocks; }
uint32_t sdcard_block_size(void)  { return s_bsize;  }

int sdcard_read_blocks(uint8_t* buf, uint32_t lba, uint32_t count) {
  if (!s_ready) return SD_ST_NO_CARD;
  return (HAL_SD_ReadBlocks(&hsd1, buf, lba, count, 5000) == HAL_OK)
         ? SD_ST_OK : SD_ST_ERR;
}

int sdcard_write_blocks(const uint8_t* buf, uint32_t lba, uint32_t count) {
  if (!s_ready) return SD_ST_NO_CARD;
  if (HAL_SD_WriteBlocks(&hsd1, (uint8_t*) buf, lba, count, 5000) != HAL_OK)
    return SD_ST_ERR;

  /* Wait until the card has finished programming the blocks. */
  uint32_t deadline = HAL_GetTick() + 5000;
  while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
    if ((int32_t)(HAL_GetTick() - deadline) >= 0) return SD_ST_ERR;
  }
  return SD_ST_OK;
}
