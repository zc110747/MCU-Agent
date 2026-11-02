/**
 ******************************************************************************
 * @file    main.c
 * @brief   STM32F429IGT6 MJPEG video player (ALIENTEK 实验47 port).
 *
 *          Target board: 正点原子 探索者 F429 + 4.3" 8080 (NT35510) panel.
 *
 *   - microSD over SDIO, DMA2 Stream6 receive, mounted as FatFs "0:"
 *   - playlist is every AVI file under 0:/VIDEO, MJPEG video + PCM audio
 *   - video surface: 800x480 RGB565 in external SDRAM, drawn through LVGL
 *   - audio: SAI1_A -> WM8978, DMA2 Stream3 double buffering
 *   - GBK fonts from 0:/SYSTEM/FONT/GBK{12,16,24,32}.FON
 *   - HAL 1 ms time base on TIM12 (pre-emption priority 0); SysTick is the
 *     FreeRTOS tick
 *
 *   INIT ORDER (hard constraints):
 *     SDRAM -> heap_5 region -> FatFs mutex -> tasks.  Nothing may touch
 *     0xC0000000 (heap, LVGL memory, video surface) before bsp_sdram_init().
 ******************************************************************************
 */
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "lvgl.h"

#include "bsp_led.h"
#include "bsp_uart.h"
#include "bsp_key.h"
#include "bsp_sdram.h"
#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "bsp_wm8978.h"
#include "lv_port_disp.h"
#include "fs_diskio.h"
#include "sd_card.h"
#include "video_ui.h"
#include "video_player.h"
#include "log.h"

/* Single SDRAM heap region, defined in sdram_heap.c. */
extern HeapRegion_t xHeapRegions[];

#define UI_TASK_STACK_WORDS  1024U
#define UI_TASK_PRIO         2U

static void SystemClock_Config(void);

void Error_Handler(void);
void vApplicationAssertFailed(const char *file, int line);

/**
 * @brief  Panel + LVGL + card + video screen, then the render pump.
 *
 *  The player task waits for video_ui_ready() and sd_card_is_ready(), so this
 *  task also owns the retry loop for a missing card: the player is parked until
 *  the screen and the volume are both up.
 */
static void ui_task(void *arg)
{
    (void)arg;

    if (lcd_driver_init() != RT_OK)
    {
        PRINT_LOG("[UI  ] LCD init FAILED - parked\r\n");
        for (;;)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }
    PRINT_LOG("[UI  ] LCD %ux%u\r\n", (unsigned int)LCD_Width, (unsigned int)LCD_Height);

    lv_init();
    lv_port_disp_init();
    PRINT_LOG("[UI  ] LVGL %d.%d.%d\r\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
              LVGL_VERSION_PATCH);

    /* Card first: the font files live on it. */
    if (sd_card_init() != RT_OK)
    {
        PRINT_LOG("[UI  ] no card, retrying\r\n");
    }
    else
    {
        (void)lcd_driver_font_init(FS_VOL_SD);
        PRINT_LOG("[UI  ] fonts: 0x%08lX\r\n",
                  (unsigned long)lcd_driver_font_status());
    }

    if (video_ui_init() != 0)
    {
        PRINT_LOG("[UI  ] video screen init FAILED\r\n");
    }

    for (;;)
    {
        bsp_key_poll();
        video_ui_lock();
        lv_timer_handler();
        video_ui_unlock();

        if ((sd_card_is_ready() == 0) && (sd_card_init() == RT_OK))
        {
            (void)lcd_driver_font_init(FS_VOL_SD);
        }

        vTaskDelay(pdMS_TO_TICKS(BSP_KEY_SCAN_PERIOD_MS));
    }
}

/* LED0 (PB1) heartbeat. */
static void led_task(void *arg)
{
    uint32_t count = 0U;

    (void)arg;
    for (;;)
    {
        count++;
        if ((count % 5U) == 0U)
        {
            BSP_LED_Toggle(0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int main(void)
{
    /* TIM12 becomes the HAL tick here (SysTick stays free for FreeRTOS). */
    HAL_Init();

    /* SYSCLK = 168 MHz, PLLQ = 7 -> 48 MHz for SDIO. */
    SystemClock_Config();

    BSP_LED_Init();
    BSP_LED_Off(0);
    BSP_LED_Off(1);

    BSP_UART_Init();
    PRINT_LOG("\r\nSTM32F429 MJPEG video player\r\n");

    bsp_key_init();

    if (bsp_sdram_init() != 0)
    {
        PRINT_LOG("SDRAM init FAILED\r\n");
        Error_Handler();
    }
    PRINT_LOG("SDRAM init OK\r\n");

    /* Heap lives in SDRAM: from here xTaskCreate / pvPortMalloc work. */
    vPortDefineHeapRegions(xHeapRegions);
    PRINT_LOG("FreeRTOS heap %u KB @0x%08lX\r\n",
              (unsigned int)(configTOTAL_HEAP_SIZE / 1024U),
              (unsigned long)(uintptr_t)xHeapRegions[0].pucStartAddress);

    if (fs_diskio_init() == 0)
    {
        PRINT_LOG("fs_diskio_init FAILED\r\n");
        Error_Handler();
    }

    /* Codec on I2C, left muted until the player opens the signal path. */
    if (bsp_wm8978_init() != 0)
    {
        PRINT_LOG("WM8978 not answering (playback will be silent)\r\n");
    }

    xTaskCreate(ui_task,           "ui",     UI_TASK_STACK_WORDS,
                NULL, UI_TASK_PRIO, NULL);
    xTaskCreate(video_player_task, "video",  VIDEO_PLAYER_STACK_WORDS,
                NULL, VIDEO_PLAYER_PRIO, NULL);
    xTaskCreate(led_task,          "led",    256U,
                NULL, tskIDLE_PRIORITY + 1U, NULL);

    /* FreeRTOS V11 leaves BASEPRI raised after the first xTaskCreate; clear it
     * so TIM12 and HAL_Delay() work before the scheduler starts. */
    __set_BASEPRI(0);
    __enable_irq();

    vTaskStartScheduler();

    Error_Handler();
    return 0;
}

/**
 * @brief  System clock @168 MHz, PLLQ = 48 MHz for SDIO.
 *   HSE = 25 MHz -> PLL (M=25, N=336, P=2 -> 168 MHz, Q=7 -> 48 MHz).
 *   180 MHz cannot produce an integer 48 MHz from this crystal (PLLQ would be
 *   7.5) and SDIO needs exactly 48 MHz, so 168 MHz it is - the same clock the
 *   104/105 projects run their SDRAM timing at.
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 25;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;)
    {
        BSP_LED_Toggle(1);
        for (volatile uint32_t i = 0; i < 0x1FFFFU; i++)
        {
        }
    }
}

void vApplicationAssertFailed(const char *file, int line)
{
    (void)file;
    (void)line;
    Error_Handler();
}

void vApplicationMallocFailedHook(void)
{
    PRINT_LOG("pvPortMalloc FAILED (SDRAM heap exhausted)\r\n");
    Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    PRINT_LOG("stack overflow: %s\r\n", name);
    Error_Handler();
}
