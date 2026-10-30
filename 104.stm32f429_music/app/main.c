/**
 ******************************************************************************
 * @file    main.c
 * @brief   STM32F429IGT6 music player: FreeRTOS + LVGL + SAI/WM8978 + SD card.
 *
 *  Data path
 *  ---------
 *    microSD (SDIO, 4-bit polled) -> FatFs -> track_decoder (WAV unpack / MP3
 *    decode) -> 16-bit stereo PCM
 *        -> DMA2 Stream3 (double buffered) -> SAI1_A (I2S master)
 *        -> WM8978 DAC -> speaker / headphone
 *
 *  Display
 *  -------
 *    800x480 panel on FMC Bank1 NE1 (8080 16-bit), driven by LVGL 8.3.
 *
 *  Time bases
 *  ----------
 *    SysTick -> FreeRTOS scheduler
 *    TIM12   -> HAL tick (HAL_Delay / HAL_GetTick / SDIO timeouts), priority 0
 *
 *  INIT ORDER (hard constraints)
 *  -----------------------------
 *    1. HAL_Init + clocks (HAL_InitTick already runs on TIM12)
 *    2. UART + LEDs (usable before any heap exists)
 *    3. SDRAM bring-up          -- nothing may touch 0xC0000000 before this
 *    4. vPortDefineHeapRegions  -- every FreeRTOS object comes from SDRAM
 *    5. FatFs lock, LCD, codec, keys
 *    6. LVGL + UI (draw buffer and LVGL heap both live in SDRAM)
 *    7. tasks, then vTaskStartScheduler
 ******************************************************************************
 */
#include "FreeRTOS.h"
#include "task.h"

#include "stm32f4xx_hal.h"

#include "bsp_delay.h"
#include "bsp_key.h"
#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "bsp_led.h"
#include "bsp_sdram.h"
#include "bsp_uart.h"
#include "bsp_wm8978.h"

#include "app_ui.h"
#include "audio_player.h"
#include "fs_diskio.h"
#include "log.h"
#include "sd_card.h"
#include "usb_msc.h"

extern HeapRegion_t xHeapRegions[];

static void system_clock_config(void);
void        error_handler(void);
void        vApplicationAssertFailed(const char *file, int line);

/* Heartbeat only.  It runs at the lowest application priority so audio and UI
 * are never delayed by it. */
static void led_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        bsp_led_toggle(0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    /* Reset peripherals and Flash interface; starts the TIM12 HAL tick. */
    HAL_Init();

    /* SYSCLK 168 MHz, PLLQ = 7 -> 48 MHz for SDIO. */
    system_clock_config();

    bsp_led_init();
    bsp_led_off(0);
    bsp_led_off(1);

    bsp_uart_init();
    PRINT_LOG("\r\n=== STM32F429 music player ===\r\n");

    /* External SDRAM must be alive before the FreeRTOS heap is defined. */
    if (bsp_sdram_init() != 0)
    {
        PRINT_LOG("[SDRAM] init FAILED\r\n");
        error_handler();
    }
    PRINT_LOG("[SDRAM] init OK\r\n");

    vPortDefineHeapRegions(xHeapRegions);

    if (!fs_diskio_init())
    {
        PRINT_LOG("[FS  ] mutex creation FAILED\r\n");
        error_handler();
    }

    if (lcd_driver_init() != RT_OK)
    {
        PRINT_LOG("[LCD ] init FAILED\r\n");
        error_handler();
    }

    /* Mount the card here (not in the audio task) so the GBK glyph files can be
     * opened before the first screen is drawn.  sd_card_init() is idempotent, so
     * the audio task's own readiness check costs nothing when this succeeded. */
    if (sd_card_init() == RT_OK)
    {
        if (lcd_driver_font_init(FS_VOL_SD) == RT_OK)
        {
            PRINT_LOG("[FONT] GBK glyph files loaded from SD\r\n");
        }
        else
        {
            PRINT_LOG("[FONT] no GBKxx.FON on SD (CJK names will not render)\r\n");
        }
    }
    else
    {
        PRINT_LOG("[SD  ] no card at boot, player will retry\r\n");
    }

    /* WM8978: playback path only (DAC -> HP + speaker). */
    if (bsp_wm8978_init() != 0U)
    {
        PRINT_LOG("[CODEC] WM8978 not responding (I2C)\r\n");
    }
    bsp_wm8978_set_volume_level(WM8978_VOLUME_LEVEL_DEF);
    bsp_wm8978_set_dac(1U);
    bsp_wm8978_set_output(1U);
    PRINT_LOG("[CODEC] WM8978 configured (volume %u/%u)\r\n",
              (unsigned)bsp_wm8978_volume_level(),
              (unsigned)WM8978_VOLUME_LEVELS);

    bsp_key_init();

    /* USB device side: exposes the SD card as a U-disk.  Needs the FreeRTOS
     * heap (its OSAL queue) and, for the first hand-over, a mounted card. */
    usb_msc_init();

    app_ui_init();
    audio_player_start();
    app_ui_start();
    usb_msc_start();

    (void)xTaskCreate(led_task, "led", 256U, NULL, tskIDLE_PRIORITY + 1, NULL);

    /* FreeRTOS V11 leaves BASEPRI set after the last pre-scheduler critical
     * section; clear it so the TIM12 tick and HAL_Delay keep working. */
    __set_BASEPRI(0);
    __enable_irq();

    vTaskStartScheduler();

    error_handler();
    return 0;
}

/**
 * @brief  System clock: HSE 25 MHz -> PLL (M=25, N=336, P=2) -> 168 MHz,
 *         PLLQ = 7 -> 48 MHz for SDIO.
 *
 *         168 MHz (not the 180 MHz maximum) is required: with HSE = 25 MHz the
 *         180 MHz PLL cannot produce an integer 48 MHz for the SDIO clock.
 */
static void system_clock_config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 25U;
    osc.PLL.PLLN       = 336U;
    osc.PLL.PLLP       = RCC_PLLP_DIV2; /* 168 MHz */
    osc.PLL.PLLQ       = 7U;            /* 48 MHz  */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        error_handler();
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4; /* 42 MHz -> TIM clock 84 MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV2; /* 84 MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK)
    {
        error_handler();
    }
}

void error_handler(void)
{
    __disable_irq();
    for (;;)
    {
        bsp_led_toggle(1);
        for (volatile uint32_t i = 0U; i < 0x1FFFFU; i++)
        {
        }
    }
}

void vApplicationAssertFailed(const char *file, int line)
{
    PRINT_LOG("[ASSERT] %s:%d\r\n", file, line);
    error_handler();
}

void vApplicationMallocFailedHook(void)
{
    PRINT_LOG("[HEAP ] allocation failed\r\n");
    error_handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    PRINT_LOG("[STACK] overflow in %s\r\n", (name != NULL) ? name : "?");
    error_handler();
}
