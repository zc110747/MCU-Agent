/* ---------------------------------------------------------------------------
* SD card driver - STM32H743 SDMMC1 (see bsp_sdcard.h for the pin map)
*
* Robustness notes
* ----------------
*  - Every block transfer first waits for the card to be back in TRANSFER
*    state (CMD13), so a card that is still programming the previous block
*    cannot poison the next command.
*  - A failed transfer is retried *inside* the driver with the SDMMC flags
*    cleared in between. Without this the USB host sees a SCSI failure and
*    Windows answers it with its 1 s-backoff error recovery, which is what
*    made the U-disk volume take ~12 s to appear after plug-in.
*  - The last HAL error code is kept in g_sd_last_err and, together with a
*    timestamped ring of interesting events, can be dumped over CDC
*    ('mtrace' command in main.c) to see exactly what the host asked for.
* -------------------------------------------------------------------------*/

#include "bsp_sdcard.h"
#include "stm32h7xx_hal.h"

#include <stdio.h>      /* snprintf (trace dump only) */

#define SDMMC_READ_WRITE_TIMEOUT        1000

#define SD_IO_TIMEOUT_MS                500   /* one block transfer          */
#define SD_IO_RETRIES                   3     /* attempts before giving up   */
#define SD_WAIT_READY_MS                100   /* wait for the card to idle   */
#define SD_PROGRAM_WAIT_MS             2000   /* write programming time      */

SD_HandleTypeDef hsd1;

typedef struct
{
    bool     ready;
    uint32_t blocks;
    uint32_t bsize;
} SD_CARD_STATE_T;

static SD_CARD_STATE_T g_sd_card = {0};

/* Last HAL error code seen by the block layer (0 = none). */
static uint32_t g_sd_last_err;

/* ------------------------------------------------------------------------ */
/* Event trace                                                              */
/*                                                                          */
/* Two 32-bit fields + two 16-bit fields per entry keeps the ring small and  */
/* the cost of one sd_trace_add() at a few instructions.                    */
/* ------------------------------------------------------------------------ */
#if SD_TRACE_ENABLE
typedef struct
{
    uint32_t tick;   /* HAL_GetTick() at the event (ms since reset)  */
    uint32_t arg;    /* tag specific: LBA, block count, capacity ... */
    uint16_t sub;    /* tag specific: error code, size, attempt      */
    uint8_t  code;   /* tag specific: result / command byte          */
    uint8_t  tag;
} SD_TRACE_ENTRY_T;

static SD_TRACE_ENTRY_T g_tr[SD_TRACE_N];
static uint16_t         g_tr_w;      /* next write slot (ring)  */
static uint16_t         g_tr_count;  /* entries stored, <= N    */

static const char* const g_tr_name[] =
{
    "?",        "BOOT",   "SDINIT", "SDREADY", "FATFS",  "TUSB",
    "MOUNT",    "UMOUNT", "TUR",    "CAP",     "RD",
    "RDOK",     "RDERR",  "WR",     "WROK",    "WRERR",
    "SCSI",     "SCSIUNK","SSU",    "WARM",
};

void sd_trace_add(uint8_t tag, uint8_t code, uint16_t sub, uint32_t arg)
{
    SD_TRACE_ENTRY_T* e = &g_tr[g_tr_w];
    e->tick = HAL_GetTick();
    e->arg  = arg;
    e->sub  = sub;
    e->code = code;
    e->tag  = tag;

    g_tr_w = (uint16_t)((g_tr_w + 1U) % SD_TRACE_N);
    if (g_tr_count < SD_TRACE_N) g_tr_count++;
}

void sd_trace_reset(void)
{
    g_tr_w     = 0;
    g_tr_count = 0;
}

bool sd_trace_next(char* out, int outsz, uint16_t* cursor)
{
    if (*cursor >= g_tr_count) return false;

    /* Ring order: the oldest entry sits `count` slots behind the writer. */
    uint16_t idx = (uint16_t)((g_tr_w + SD_TRACE_N - g_tr_count + *cursor) % SD_TRACE_N);
    const SD_TRACE_ENTRY_T* e = &g_tr[idx];

    const char* name = (e->tag < (sizeof(g_tr_name) / sizeof(g_tr_name[0])))
                       ? g_tr_name[e->tag] : "?";

    (void) snprintf(out, (size_t) outsz, "%6lu ms  %-8s code=%02X sub=%04X arg=%lu\r\n",
                    (unsigned long) e->tick, name,
                    (unsigned) e->code, (unsigned) e->sub,
                    (unsigned long) e->arg);

    (*cursor)++;
    return true;
}
#else
void sd_trace_add(uint8_t tag, uint8_t code, uint16_t sub, uint32_t arg)
{
    (void) tag; (void) code; (void) sub; (void) arg;
}
void sd_trace_reset(void) {}
bool sd_trace_next(char* out, int outsz, uint16_t* cursor)
{
    (void) out; (void) outsz; (void) cursor;
    return false;
}
#endif

/* ------------------------------------------------------------------------ */
/* HAL MSP hook - runs from inside HAL_SD_Init()                            */
/* ------------------------------------------------------------------------ */
void HAL_SD_MspInit(SD_HandleTypeDef* hsd)
{
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

void HAL_SD_MspDeInit(SD_HandleTypeDef* hsd)
{
    if (hsd->Instance != SDMMC1) return;
    __HAL_RCC_SDMMC1_CLK_DISABLE();
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

/* Wait until the card reports the TRANSFER state (a no-op CMD13). A card
* that is still programming a block answers "busy", and issuing CMD17/CMD24
* to a busy card is what produces the sporadic first-command failures. */
static HAL_StatusTypeDef sd_wait_card(uint32_t timeout_ms)
{
    uint32_t const deadline = HAL_GetTick() + timeout_ms;

    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
    {
        if ((int32_t)(HAL_GetTick() - deadline) >= 0) return HAL_TIMEOUT;
    }
    return HAL_OK;
}

/* ------------------------------------------------------------------------ */
void sdcard_init(void)
{
    HAL_SD_CardInfoTypeDef info;

    g_sd_card.ready = false;
    g_sd_card.blocks = 0;
    g_sd_card.bsize  = 0;

    /* DeInit only touches the peripheral when the handle was already bound;
    * on the first call Instance is NULL and HAL would poke address 0. */
    if (hsd1.Instance == SDMMC1) (void) HAL_SD_DeInit(&hsd1);

    hsd1.Instance = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    /* SDMMC_CK = PLL1Q(100 MHz) / (2 * ClockDiv) = 100 / 4 = 25 MHz.
    * HAL drops this to <=400 kHz on its own for the card-identification phase. */
    hsd1.Init.ClockDiv            = 2;

    if (HAL_SD_Init(&hsd1) != HAL_OK)
    {
        sd_trace_add(SD_TR_SDINIT, 1, (uint16_t) hsd1.ErrorCode, 0);
        return;
    }
    if (HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) != HAL_OK)
    {
        sd_trace_add(SD_TR_SDINIT, 2, (uint16_t) hsd1.ErrorCode, 0);
        return;
    }

    if (HAL_SD_GetCardInfo(&hsd1, &info) == HAL_OK)
    {
        g_sd_card.blocks = info.LogBlockNbr;
        g_sd_card.bsize  = info.LogBlockSize;   /* 512 */
    }
    g_sd_card.ready = (g_sd_card.blocks > 0);

    sd_trace_add(SD_TR_SDREADY, g_sd_card.ready ? 1U : 0U,
                 (uint16_t) g_sd_card.bsize, g_sd_card.blocks);

    /* Warm-up: the very first CMD17 after power-up can come back with a CRC
    * or timeout error on some cards. Absorb it here, before the USB host is
    * allowed to see the disk, instead of letting Windows retry for seconds. */
    if (g_sd_card.ready)
    {
        uint8_t warm[512];
        for (uint32_t i = 0; i < SD_IO_RETRIES; i++)
        {
            int32_t st = sdcard_read_blocks(warm, 0, 1);
            sd_trace_add(SD_TR_WARM, (uint8_t) i, 0, (uint32_t) st);
            if (st == SD_ST_OK) break;
        }
    }
}

bool sdcard_present(void)
{
    return g_sd_card.ready;
}

uint32_t sdcard_block_count(void)
{
    return g_sd_card.blocks;
}
uint32_t sdcard_block_size(void)
{
    return g_sd_card.bsize;
}
uint32_t sdcard_last_error(void)
{
    return g_sd_last_err;
}

int32_t sdcard_read_blocks(uint8_t* buf, uint32_t lba, uint32_t count)
{
    if (!g_sd_card.ready) return SD_ST_NO_CARD;
    if (count == 0)       return SD_ST_OK;

    for (uint32_t attempt = 0; attempt < SD_IO_RETRIES; attempt++)
    {
        (void) sd_wait_card(SD_WAIT_READY_MS);

        if (HAL_SD_ReadBlocks(&hsd1, buf, lba, count, SD_IO_TIMEOUT_MS) == HAL_OK)
        {
            return SD_ST_OK;
        }

        g_sd_last_err = hsd1.ErrorCode;
        __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_FLAGS);
        sd_trace_add(SD_TR_RDERR, (uint8_t) attempt,
                     (uint16_t) g_sd_last_err, lba);
    }
    return SD_ST_ERR;
}

int32_t sdcard_write_blocks(const uint8_t* buf, uint32_t lba, uint32_t count)
{
    if (!g_sd_card.ready) return SD_ST_NO_CARD;
    if (count == 0)       return SD_ST_OK;

    for (uint32_t attempt = 0; attempt < SD_IO_RETRIES; attempt++)
    {
        (void) sd_wait_card(SD_WAIT_READY_MS);

        if (HAL_SD_WriteBlocks(&hsd1, (uint8_t*) buf, lba, count,
                               SD_IO_TIMEOUT_MS) == HAL_OK)
        {
            /* Wait until the card has finished programming the blocks. */
            if (sd_wait_card(SD_PROGRAM_WAIT_MS) == HAL_OK) return SD_ST_OK;
        }

        g_sd_last_err = hsd1.ErrorCode;
        __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_FLAGS);
        sd_trace_add(SD_TR_WRERR, (uint8_t) attempt,
                     (uint16_t) g_sd_last_err, lba);
    }
    return SD_ST_ERR;
}

HAL_StatusTypeDef sdcard_read_disk(uint8_t *buf, uint32_t startBlocks,
                                   uint32_t NumberOfBlocks)
{
    return (sdcard_read_blocks(buf, startBlocks, NumberOfBlocks) == SD_ST_OK)
           ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef sdcard_write_disk(const uint8_t *buf, uint32_t startBlocks,
                                    uint32_t NumberOfBlocks)
{
    return (sdcard_write_blocks(buf, startBlocks, NumberOfBlocks) == SD_ST_OK)
           ? HAL_OK : HAL_ERROR;
}
