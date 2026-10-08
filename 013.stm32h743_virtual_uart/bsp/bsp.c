/* ---------------------------------------------------------------------------
 * Board support - STM32H743ZIT6, external 25 MHz crystal
 *
 *   HSE 25 MHz -> /M=2 -> 12.5 MHz -> PLL1 xN=64 -> 800 MHz VCO -> /P=2
 *                                                              -> SYSCLK 400 MHz
 *   HCLK = SYSCLK/2 = 200 MHz, APB1/2/3/4 = 100 MHz
 *
 * Cache policy: D-cache stays ON. The software-UART DMA descriptors live in
 * AXI SRAM (.dma_buf) and are bracketed with explicit Clean/Invalidate-by-
 * address calls, so no MPU carve-out is needed.
 * -------------------------------------------------------------------------*/

#include "bsp.h"
#include "stm32h7xx_hal.h"

uint32_t g_sysclk_hz;
uint32_t g_hclk_hz;
uint32_t g_pclk1_hz;

static void error_trap(void)
{
    __disable_irq();
    while (1)
    {
        /* hang here - attach the debugger to see the caller */
    }
}

/* ------------------------------------------------------------------------ */
static void system_clock_config(void)
{
    RCC_OscInitTypeDef osc  = {0};
    RCC_ClkInitTypeDef clk  = {0};

    /* 1. LDO supply (reset default, stated explicitly to match the hardware) */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    /* 2. VOS1 supports up to 400 MHz */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    /* 3. HSE (25 MHz passive crystal). HSI is left on: right after reset it
     *    is the system clock, and HAL refuses to disable the current source. */
    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState            = RCC_HSE_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

    osc.PLL.PLLState  = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM      = 2;                    /* 25 MHz / 2  = 12.5 MHz */
    osc.PLL.PLLN      = 64;                   /* 12.5 * 64   = 800 MHz  */
    osc.PLL.PLLP      = 2;                    /* 800 / 2     = 400 MHz  */
    osc.PLL.PLLQ      = 8;                    /* 100 MHz spare          */
    osc.PLL.PLLR      = 2;
    osc.PLL.PLLRGE    = RCC_PLL1VCIRANGE_3;   /* input 8..16 MHz        */
    osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;      /* VCO 192..836 MHz       */
    osc.PLL.PLLFRACN  = 0;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        error_trap();
    }

    /* 4. Bus clocks */
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK    |
                    RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1  |
                    RCC_CLOCKTYPE_PCLK2   | RCC_CLOCKTYPE_D3PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;   /* CPU 400 MHz */
    clk.AHBCLKDivider  = RCC_HCLK_DIV2;     /* AHB 200 MHz */
    clk.APB3CLKDivider = RCC_APB3_DIV2;     /* 100 MHz     */
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK)
    {
        error_trap();
    }

    g_sysclk_hz = HAL_RCC_GetSysClockFreq();
    g_hclk_hz   = HAL_RCC_GetHCLKFreq();
    g_pclk1_hz  = HAL_RCC_GetPCLK1Freq();
}

/* ------------------------------------------------------------------------ */
static void led_init(void)
{
    GPIO_InitTypeDef g = {0};

    LED_GPIO_CLK_EN();
    g.Pin   = LED_GPIO_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &g);
    board_led_write(false);
}

void board_led_write(bool on)
{
#if LED_ACTIVE_HIGH
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif
}

void board_led_toggle(void)
{
    HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_GPIO_PIN);
}

uint32_t board_millis(void)
{
    return HAL_GetTick();
}

void board_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/* ------------------------------------------------------------------------ */
void bsp_init(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();

    HAL_Init();                  /* priority grouping + 1 ms SysTick timebase */
    system_clock_config();

    led_init();
}
