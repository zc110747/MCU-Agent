/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F429IGT6 + FreeRTOS + USB FS Host (TinyUSB) + FatFs exFAT.
  *
  *   - USB OTG FS Host reads a U-disk (MSC -> SCSI -> FatFs)
  *   - exFAT supported by ChaN FatFs R0.15 (FF_FS_EXFAT=1) -- real, not faked
 *   - FreeRTOS heap (heap_5, single region) lives in external SDRAM
 *   - HAL 1 ms time base on TIM7 (SysTick belongs to FreeRTOS)
 *   - microSD over SDIO (4-bit, polled) mounted as FatFs volume "1:"
 *   - USB MSC U-disk mounted as FatFs volume "0:" (fallback font source)
 *   - USART3 (PB10/PB11) debug console
 *
 *   INIT ORDER (hard constraints):
 *     - SDRAM + FreeRTOS heap MUST exist before any FreeRTOS object
 *     - USB HW + stack init MUST run AFTER vTaskStartScheduler (inside
 *       usbh_host_task); the OTG FS ISR uses FreeRTOS FromISR queue calls
 *       that are only valid once the scheduler is running
 *     HAL_Init -> TIM7 tick -> SystemClock(168MHz/48MHz USB)
 *     -> GPIO/LED -> UART -> I2C -> PCF8574 -> SDRAM init+test
 *     -> vPortDefineHeapRegions(SDRAM) -> fs_diskio_init(mutex)
 *     -> usbh_app_init(sem + file_task)
 *     -> create tasks -> vTaskStartScheduler
 *     (then) usbh_host_task: USBH_HW_Init + tusb_init -> tuh_task loop
 ******************************************************************************
 */
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>

#include "bsp_led.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "bsp_pcf8574.h"
#include "bsp_sdram.h"
#include "usb_host_app.h"
#include "fs_diskio.h"
#include "ui_task.h"
#include "touch_task.h"
#include "sensor_task.h"
#include "log.h"

/* Single SDRAM heap region, defined in sdram_heap.c. */
extern HeapRegion_t xHeapRegions[];

static void SystemClock_Config(void);
void Error_Handler(void);
void vApplicationAssertFailed(const char *file, int line);

/* ---- LED task: LED0 heartbeat, LED1 reflects USB state ---- */
static void led_task(void *arg)
{
  (void)arg;
  int cnt = 0;
  for (;;)
  {
    cnt++;
    if (cnt % 5 == 0) BSP_LED_Toggle(0);   /* ~500 ms heartbeat */

    switch (g_usb_state)
    {
      case USB_MOUNTED:
        BSP_LED_On(1);
        break;
      case USB_ERROR:
        BSP_LED_Toggle(1);                 /* ~100 ms fast blink */
        break;
      case USB_ENUMERATED:
      case USB_MSC_READY:
        if (cnt % 4 == 0) BSP_LED_Toggle(1); /* ~400 ms slow blink */
        break;
      default:
        BSP_LED_Off(1);
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

int main(void)
{
  /* Reset peripherals, Flash interface, and the TIM7 HAL tick. */
  HAL_Init();

  /* SYSCLK=168 MHz, HSE=25 MHz, PLLQ=7 -> 48 MHz USB (must be exact). */
  SystemClock_Config();

  /* Board status LEDs (PB0/PB1, low-active). */
  BSP_LED_Init();
  BSP_LED_Off(0);
  BSP_LED_Off(1);

  /* Debug console (USART3 PB10/PB11).  No FreeRTOS object is created here,
   * so it is safe to use before SDRAM / the FreeRTOS heap is up. */
  BSP_UART_Init();
  PRINT_LOG("\r\nSystem Init\r\n");

  /* I2C2 (PH4/PH5) -> PCF8574 expander.  Recover bus first (SDA may be stuck
   * low from a previous reset), then release ETH_PHY from reset (P7=0 ->
   * ETH_RESET=1) and keep BEEP off (P0=1).  This only touches I2C; it must
   * not disturb USB/FMC/FreeRTOS. */
  BSP_I2C_Init();
  BSP_I2C_Recover();
  BSP_PCF8574_Write(0x7FU);
  PRINT_LOG("I2C / PCF8574 init OK\r\n");

  /* ---- External SDRAM MUST come up before any FreeRTOS object ---- */
  PRINT_LOG("SDRAM Init ...\r\n");
  if (bsp_sdram_init() != 0)
  {
    PRINT_LOG("SDRAM Init FAILED\r\n");
    Error_Handler();
  }
  PRINT_LOG("SDRAM Init OK\r\n");

  /* Register the SDRAM region with heap_5.  From now on xTaskCreate /
   * pvPortMalloc allocate from ucHeap @0xC0000000.  No earlier FreeRTOS
   * allocation is allowed (would corrupt the heap free list). */
  vPortDefineHeapRegions(xHeapRegions);
  PRINT_LOG("FreeRTOS Heap configured (SDRAM @0xC0000000)\r\n");

  /* FatFs serialization mutex (shared by the USB MSC and microSD transports).
   * Must exist before any task touches a filesystem. */
  if (!fs_diskio_init())
  {
    PRINT_LOG("fs_diskio_init FAILED\r\n");
    Error_Handler();
  }

  /* ---- USB FS Host: hardware + stack init is DEFERRED to usbh_host_task,
   *      which only runs after vTaskStartScheduler().  Reason: tusb_init()
   *      enables the OTG FS interrupt, and its ISR uses FreeRTOS FromISR
   *      queue calls that are only valid once the scheduler is running.
   *      Initializing here (pre-scheduler) lets the interrupt fire into a
   *      not-yet-live RTOS and corrupt system state. ---- */

  if (!usbh_app_init())
  {
    PRINT_LOG("usbh_app_init FAILED\r\n");
    Error_Handler();
  }

  PRINT_LOG("Waiting for USB disk...\r\n");

  /* Create tasks (all allocate from the SDRAM heap, now valid).
   *
   *  Priorities, highest first:
   *    +4 touch      - must publish a press before the next LVGL read
   *    +3 usbh       - host stack deadlines
   *    +2 ui         - LVGL render pump
   *    +1 led/sensor - heartbeat and I2C sampling
   */
  xTaskCreate(usbh_host_task, "usbh", 1024, NULL, tskIDLE_PRIORITY + 3, NULL);
  xTaskCreate(touch_task,     "touch", TOUCH_TASK_STACK_WORDS, NULL,
              TOUCH_TASK_PRIO, NULL);
  xTaskCreate(sensor_task,    "sensor", SENSOR_TASK_STACK_WORDS, NULL,
              SENSOR_TASK_PRIO, NULL);
  xTaskCreate(led_task,       "led",  256,  NULL, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(ui_task,        "ui",   UI_TASK_STACK_WORDS, NULL,
             tskIDLE_PRIORITY + 2, NULL);

  /* FreeRTOS V11 quirk: vPortEnterCritical (inside xTaskCreate before the
   * scheduler runs) leaves BASEPRI set; clear it so the TIM7 IRQ and
   * HAL_Delay work after this point. */
  __set_BASEPRI(0);
  __enable_irq();

  vTaskStartScheduler();

  /* Should never reach here */
  Error_Handler();
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
  * @brief  System Clock @168 MHz, USB 48 MHz.
  *   HSE=25 MHz -> PLL (M=25, N=336, P=2 -> 168 MHz, Q=7 -> 48 MHz).
  *   NOTE: 180 MHz (the F429 max) cannot yield an integer 48 MHz from the
  *   same PLL (VCO/PLLQ with HSE=25 would need PLLQ=7.5), so we use 168 MHz
  *   to guarantee a clean, spec-compliant 48 MHz USB clock.
  */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;   /* 168 MHz */
  RCC_OscInitStruct.PLL.PLLQ = 7;               /* 48 MHz (USB) */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;  /* 42 MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;  /* 84 MHz */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
    BSP_LED_Toggle(1);
    for (volatile uint32_t i = 0; i < 0x1FFFF; i++) { }
  }
}

void vApplicationAssertFailed(const char *file, int line)
{
  (void)file; (void)line;
  Error_Handler();
}

void vApplicationMallocFailedHook(void)
{
  Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask; (void)pcTaskName;
  Error_Handler();
}
