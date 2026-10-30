/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F429IGT6 touchpad demo: LVGL 8.3 on an 800x480 NT35510,
  *          GT9147 multi-touch gestures, fonts + images stored on microSD.
  *
  *   - microSD over SDIO (4-bit, polled, PC8..PC12/PD2) mounted as FatFs "0:"
  *   - GBK glyph files  0:/SYSTEM/FONT/GBK{12,16,24,32}.FON  (no CJK in flash)
  *   - LVGL images      0:/IMG/<name>.bin  (wallpaper + icons)
  *   - FreeRTOS heap (heap_5, single region) lives in external SDRAM
  *   - HAL 1 ms time base on TIM7 (SysTick belongs to FreeRTOS)
  *   - USART1 (PA9/PA10) debug console, on-board CH340, 115200 8N1
  *
  *   INIT ORDER (hard constraints):
  *     - SDRAM + FreeRTOS heap MUST exist before any FreeRTOS object
  *     - the FatFs mutex must exist before any task touches the card
  *     - the panel must be up before the touch driver can map coordinates
  *       (that ordering is enforced inside bsp_touch_init(), which waits for
  *        lcd_driver_ready() instead of assuming anything about task order)
  *
  *       HAL_Init -> TIM7 tick -> SystemClock(168 MHz, PLLQ=7 -> 48 MHz SDIO)
  *       -> LED -> UART -> keys -> SDRAM init + test
  *       -> vPortDefineHeapRegions(SDRAM) -> fs_diskio_init(mutex)
  *       -> create tasks -> vTaskStartScheduler
  ******************************************************************************
  */
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>

#include "bsp_led.h"
#include "bsp_uart.h"
#include "bsp_key.h"
#include "bsp_sdram.h"
#include "fs_diskio.h"
#include "gesture.h"
#include "touch_task.h"
#include "ui_task.h"
#include "log.h"

/* Single SDRAM heap region, defined in sdram_heap.c. */
extern HeapRegion_t xHeapRegions[];

static void SystemClock_Config(void);
void Error_Handler(void);
void vApplicationAssertFailed(const char *file, int line);

/* LED0 (PB1) heartbeat.  LED1 is left for a future error indication. */
static void led_task(void *arg)
{
  uint32_t cnt = 0U;

  (void)arg;
  for (;;)
  {
    cnt++;
    if ((cnt % 5U) == 0U)
    {
      BSP_LED_Toggle(0);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

int main(void)
{
  /* Reset peripherals, Flash interface and the TIM7 HAL tick. */
  HAL_Init();

  /* SYSCLK = 168 MHz, PLLQ = 7 -> 48 MHz for SDIO (and USB, unused here). */
  SystemClock_Config();

  BSP_LED_Init();
  BSP_LED_Off(0);
  BSP_LED_Off(1);

  /* Debug console.  Creates no FreeRTOS object, so it works before the heap. */
  BSP_UART_Init();
  PRINT_LOG("\r\nSTM32F429 touchpad demo\r\n");

  /* Four on-board keys, polled by the UI task (no interrupt). */
  bsp_key_init();

  /* ---- External SDRAM must come up before any FreeRTOS object ---- */
  PRINT_LOG("SDRAM Init ...\r\n");
  if (bsp_sdram_init() != 0)
  {
    PRINT_LOG("SDRAM Init FAILED\r\n");
    Error_Handler();
  }
  PRINT_LOG("SDRAM Init OK\r\n");

  /* Register the SDRAM region with heap_5.  From here on xTaskCreate /
   * pvPortMalloc allocate from ucHeap @0xC0000000. */
  vPortDefineHeapRegions(xHeapRegions);
  PRINT_LOG("FreeRTOS heap: %u KB @0x%08lX (SDRAM)\r\n",
            (unsigned int)(configTOTAL_HEAP_SIZE / 1024U),
            (unsigned long)(uintptr_t)xHeapRegions[0].pucStartAddress);

  /* FatFs serialization mutex: must exist before the UI task probes the card. */
  if (fs_diskio_init() == 0)
  {
    PRINT_LOG("fs_diskio_init FAILED\r\n");
    Error_Handler();
  }

  gesture_init();

  /* Priorities, highest first:
   *   +4 touch  - publish a press before the next LVGL read
   *   +2 ui     - LVGL render pump + gesture drain
   *   +1 led    - heartbeat
   */
  xTaskCreate(touch_task, "touch", TOUCH_TASK_STACK_WORDS, NULL, TOUCH_TASK_PRIO, NULL);
  xTaskCreate(ui_task,    "ui",    UI_TASK_STACK_WORDS,    NULL, UI_TASK_PRIO,    NULL);
  xTaskCreate(led_task,   "led",   256U,                  NULL, tskIDLE_PRIORITY + 1U, NULL);

  /* FreeRTOS V11 quirk: vPortEnterCritical (inside xTaskCreate before the
   * scheduler runs) leaves BASEPRI set; clear it so TIM7 and HAL_Delay work. */
  __set_BASEPRI(0);
  __enable_irq();

  vTaskStartScheduler();

  /* Should never get here. */
  Error_Handler();
  return 0;
}

void bsp_delay_ms(uint32_t ms)
{
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
  {
    vTaskDelay(pdMS_TO_TICKS(ms));
  }
  else
  {
    HAL_Delay(ms);
  }
}

/**
  * @brief  System clock @168 MHz, PLLQ = 48 MHz for SDIO.
  *   HSE = 25 MHz -> PLL (M=25, N=336, P=2 -> 168 MHz, Q=7 -> 48 MHz).
  *   180 MHz (the F429 maximum) cannot produce an integer 48 MHz from this
  *   crystal (it would need PLLQ = 7.5), and SDIO needs exactly 48 MHz.
  */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM       = 25;
  RCC_OscInitStruct.PLL.PLLN       = 336;
  RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;   /* 168 MHz */
  RCC_OscInitStruct.PLL.PLLQ       = 7;               /* 48 MHz (SDIO) */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;   /* 42 MHz  */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;   /* 84 MHz  */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
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
    for (volatile uint32_t i = 0; i < 0x1FFFFU; i++) { }
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

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  PRINT_LOG("stack overflow: %s\r\n", pcTaskName);
  Error_Handler();
}
