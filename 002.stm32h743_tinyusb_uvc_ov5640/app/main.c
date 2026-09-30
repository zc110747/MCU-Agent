/**
 ******************************************************************************
 * @file    main.c
 * @brief   STM32H743ZIT6 + OV5640 -> USB UVC camera (TinyUSB device stack).
 *
 * Boot sequence
 * -------------
 *   MPU  : mark AXI SRAM (frame buffers) non-cacheable
 *   Cache: enable I-cache and D-cache
 *   HAL  : SysTick @ 1 kHz
 *   Clock: HSE 25 MHz -> 480 MHz SYSCLK, PLL1Q = 48 MHz for USB OTG FS
 *   LED  : PG7 heartbeat
 *   Cam  : OV5640 over I2C4 (PF14/PF15), DCMI 8-bit + DMA2_Stream1
 *   USB  : OTG_FS on PA11/PA12, rhport 0
 *
 * LED code
 * --------
 *   slow blink (500 ms) : idle, waiting for the host to start streaming
 *   fast blink (100 ms) : streaming frames
 *   very fast  (50 ms)  : sensor not detected, streaming a test pattern
 ******************************************************************************
 */

#include "main.h"
#include "bsp_board.h"
#include "bsp_camera.h"
#include "uvc_app.h"
#include "tusb.h"

/* Module-level state. cam_status / cam_id are the sensor bring-up result the
 * debugger can read at a glance; loop_count is the super-loop heartbeat. None of
 * these are referenced from another translation unit, so they are file-static. */
typedef struct
{
    cam_status_t cam_status; /* sensor bring-up result (CAM_OK or an error) */
    uint32_t     cam_id;     /* OV5640 chip id read back over SCCB          */
    uint32_t     loop_count; /* super-loop iterations                      */
} app_state_t;

static app_state_t s_app = {
    .cam_status = CAM_ERR_SENSOR,
};

/* ==========================================================================
 * USB OTG FS low level init
 *
 * TinyUSB's dwc2 port does not touch GPIOs or peripheral clocks - that is the
 * board's job. On the H743, rhport 0 maps to USB2_OTG_FS (0x40080000), whose
 * DM/DP pins are PA11/PA12 with alternate function 10.
 * ========================================================================== */
static void board_usb_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_11 | GPIO_PIN_12; /* DM / DP */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG2_FS;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* Peripheral clock. The ULPI clock must stay off for the embedded PHY. */
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
    __HAL_RCC_USB2_OTG_FS_ULPI_CLK_DISABLE();

    /* Priority must be numerically >= the FreeRTOS-style threshold; with no RTOS
     * we only need it to be lower priority than the DCMI/DMA interrupts (5). */
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);
    /* NVIC_EnableIRQ is done by TinyUSB in tud_init() -> dcd_int_enable(). */
}

/* ==========================================================================
 * TinyUSB time hooks (CFG_TUSB_OS == OPT_OS_NONE)
 * ========================================================================== */
uint32_t tusb_time_millis_api(void)
{
    return HAL_GetTick();
}

void tusb_time_delay_ms_api(uint32_t ms)
{
    HAL_Delay(ms);
}

/* ==========================================================================
 * main
 * ========================================================================== */
int main(void)
{
    /* The MPU must be programmed before the caches are turned on. */
    bsp_mpu_config();
    bsp_cache_enable();

    HAL_Init();
    bsp_clock_config();
    SystemCoreClockUpdate();

    bsp_led_init();
    bsp_led_set(true);

    /* Bring up the camera. A failure is not fatal: we fall back to a synthetic
     * test pattern so the USB side can still be brought up and debugged. */
    s_app.cam_status = bsp_camera_init();
    s_app.cam_id     = bsp_camera_get_id();
    if (s_app.cam_status == CAM_OK)
    {
        bsp_camera_link_dma_callbacks();
    }

    board_usb_init();

    const tusb_rhport_init_t usb_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(BOARD_TUD_RHPORT, &usb_init);

    uvc_app_init(s_app.cam_status == CAM_OK);

    bsp_led_set(false);

    while (1)
    {
        s_app.loop_count++;

        tud_task();     /* TinyUSB device stack */
        uvc_app_task(); /* capture + frame submission */

        /* Capture housekeeping: polls the DCMI error flags we deliberately keep
         * out of the interrupt path, restarts a wedged sensor, and services the
         * debugger-driven test-pattern toggle. Runs even when idle. */
        if (s_app.cam_status == CAM_OK)
        {
            bsp_camera_service();
        }

        uint32_t blink_ms;
        if (s_app.cam_status != CAM_OK)
        {
            blink_ms = 50; /* sensor problem */
        }
        else if (uvc_app_is_streaming())
        {
            blink_ms = 100; /* streaming */
        }
        else
        {
            blink_ms = 500; /* idle */
        }
        bsp_led_task(blink_ms);
    }
}

/* ==========================================================================
 * Error handling
 * ========================================================================== */
void Error_Handler(void)
{
    __disable_irq();

    /* Blink furiously so a hard failure is visible without a debugger. */
    LED_RUN_CLK_ENABLE();
    while (1)
    {
        LED_RUN_PORT->ODR ^= LED_RUN_PIN;
        for (volatile uint32_t i = 0; i < 1000000U; i++)
        {
            __NOP();
        }
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
