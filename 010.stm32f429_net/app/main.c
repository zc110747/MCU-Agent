/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F429IGT6 + FreeRTOS + LwIP + SDRAM network demo.
  *          - LAN8720 PHY (released from reset via PCF8574 P7)
  *          - Static IPv4 192.168.10.99, tcpip_thread drives LwIP
  *          - HTTP/HTTPS server (netconn API) on port 80/443
  *          - Web pages built-in as const arrays (no SD card)
  *          - External SDRAM 32 MB @0xC0000000: FreeRTOS heap + LwIP pools
  *          - HAL 1 ms time base on TIM7 (SysTick belongs to FreeRTOS)
  *          - LED heartbeat task, USART1(PA9/PA10) debug console
  ******************************************************************************
  */
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#include "bsp_led.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "bsp_pcf8574.h"
#include "bsp_sdram.h"
#include "bsp_ap3216.h"
#include "bsp_mpu9250.h"
#include "web_serve.h"
#include "netcfg.h"
#include "lwip.h"
#include "http_server.h"
#include "log.h"
#include "https_server.h"
#include "hwinfo.h"
#include "shell.h"

static void SystemClock_Config(void);
void Error_Handler(void);

/* ---- LED heartbeat task ---- */
static void led_task(void *arg)
{
  (void)arg;
  for (;;)
  {
    BSP_LED_Toggle(0);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

int main(void)
{
  /* Reset of all peripherals, initializes the Flash interface and TIM7 tick */
  HAL_Init();

  /* Configure the system clock to 180 MHz (HSE 25 MHz -> PLL) */
  SystemClock_Config();

  /* External SDRAM MUST come up first. FreeRTOS heap (ucHeap) and LwIP pools
   * live in SDRAM, and every xTaskCreate / xSemaphoreCreate* allocates from
   * ucHeap. Creating any of those before SDRAM is initialized writes the
   * object's control block into uninitialized memory and corrupts the heap_4
   * free list (heap_4.c:269 subtract-underflow assert). Bringing SDRAM up here
   * lets every later init create FreeRTOS objects safely, with no split. */
  if (bsp_sdram_init() != 0)
  {
    Error_Handler();
  }

  /* Debug console */
  BSP_UART_Init();
  PRINT_LOG("\r\nSTM32F429 NET demo starting (FreeRTOS + LwIP)...\r\n");
  shell_init();

  /* Board status LED. LED0/PB1 is the heartbeat (driven by led_task);
   * LED1/PB0 is web-controlled and starts OFF so the web API owns it. */
  BSP_LED_Init();
  BSP_LED_Off(1);

  /* I2C2 + PCF8574 used to release the ETH PHY from reset */
  BSP_I2C_Init();
  BSP_ETH_PHY_Reset();
  PRINT_LOG("ETH PHY (LAN8720) released from reset.\r\n");

  /* Runtime netcfg defaults (in-RAM only; no SD persistence) */
  web_serve_init();

  /* Sensors on I2C2 (AP3216C light/proximity + MPU9250 9-axis) */
  if (bsp_ap3216c_init() == 0) PRINT_LOG("AP3216C: init OK\r\n");
  else                         PRINT_LOG("AP3216C: init FAIL\r\n");
  if (bsp_mpu9250_init() == 0) PRINT_LOG("MPU9250: init OK\r\n");
  else                         PRINT_LOG("MPU9250: init FAIL\r\n");

  /* LwIP (FreeRTOS mode): tcpip_thread + netif + link task */
  MX_LWIP_Init();
  /* FreeRTOS V11 quirk: xTaskCreate before vTaskStartScheduler leaves
   * BASEPRI set to configMAX_SYSCALL_INTERRUPT_PRIORITY (vPortEnterCritical
   * inside, vPortExitCritical gated on xSchedulerRunning). Clear it here so
   * TIM7 IRQ and HAL_Delay still work for any subsequent pre-scheduler
   * syscalls. */
  __set_BASEPRI(0);
  __enable_irq();

  /* HTTP (port 80) + HTTPS (port 443, mbedTLS) server tasks */
  http_server_init();
  __set_BASEPRI(0);
  __enable_irq();
  https_server_init();
  __set_BASEPRI(0);
  __enable_irq();

  /* Shared hardware info collector (web/telnet/snmp read from it).
   * Spawns hwinfo_task at HWINFO_PERIOD_MS; the task runs after the
   * scheduler starts. */
  hwinfo_init();

  PRINT_LOG("FreeRTOS scheduler starting... (IP 192.168.10.99)\r\n");

  /* Create the LED heartbeat task, then start the scheduler */
  xTaskCreate(led_task, "led", 128, NULL, tskIDLE_PRIORITY + 1, NULL);
  vTaskStartScheduler();

  /* Should never reach here */
  Error_Handler();
}

/**
  * @brief  System Clock Configuration
  *         The system Clock is configured as follow:
  *            System Clock source            = PLL (HSE)
  *            SYSCLK(Hz)                     = 180000000
  *            HCLK(Hz)                       = 180000000
  *            AHB Prescaler                  = 1
  *            APB1 Prescaler                 = 4 (45 MHz)
  *            APB2 Prescaler                 = 2 (90 MHz)
  *            HSE Frequency(Hz)              = 25000000
  *            PLL_M                          = 25
  *            PLL_N                          = 360
  *            PLL_P                          = 2
  *            PLL_Q                          = 8 (45 MHz for I2S)
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
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 8;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

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
