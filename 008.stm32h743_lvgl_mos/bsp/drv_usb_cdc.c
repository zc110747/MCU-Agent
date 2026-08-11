/**
  ******************************************************************************
  * @file    drv_usb_cdc.c
  * @brief   USB CDC-ACM virtual COM port on USB2_OTG_FS (PA11 = DM, PA12 = DP).
  *
  *  Clocking
  *  --------
  *  The OTG core needs a 48 MHz reference that is accurate to 0.25 %, so it is
  *  taken from PLL3-Q rather than the free running HSI48: PLL3 shares its
  *  input mux with PLL1, therefore it inherits whichever oscillator actually
  *  came up (see SystemClock_Config), and the divider pair is chosen to hit
  *  480 MHz VCO / 48 MHz Q in both cases:
  *
  *      HSE 25 MHz : M = 5  -> 5 MHz, N = 96  -> 480 MHz, Q = 10 -> 48 MHz
  *      HSI 64 MHz : M = 16 -> 4 MHz, N = 120 -> 480 MHz, Q = 10 -> 48 MHz
  *
  *  If the crystal died, USB still works - it just inherits the HSI's error,
  *  which is usually good enough to enumerate.
  *
  *  Transceiver
  *  -----------
  *  The internal FS PHY needs its own 3.3 V supply enabled
  *  (HAL_PWREx_EnableUSBVoltageDetector) before any OTG register is touched;
  *  skipping it is the classic "device never enumerates" bug on the H7.
  ******************************************************************************
  */
#include "drv_usb_cdc.h"
#include "main.h"
#include "tusb.h"

#include <string.h>

static uint8_t s_ready = 0U;    /* stack initialised            */
static uint8_t s_dtr   = 0U;    /* host opened the port         */

/**
  * @brief  Route a crystal-accurate 48 MHz to the OTG core through PLL3-Q.
  */
static int usb_clock_config(void)
{
    RCC_PeriphCLKInitTypeDef periph = {0};

    periph.PeriphClockSelection = RCC_PERIPHCLK_USB;

    if (g_clock_source == CLOCK_SRC_HSE_XTAL)
    {
        periph.PLL3.PLL3M = 5U;     /* 25 MHz / 5  = 5 MHz  */
        periph.PLL3.PLL3N = 96U;    /* 5 MHz  * 96 = 480 MHz */
    }
    else
    {
        periph.PLL3.PLL3M = 16U;    /* 64 MHz / 16 = 4 MHz  */
        periph.PLL3.PLL3N = 120U;   /* 4 MHz * 120 = 480 MHz */
    }

    periph.PLL3.PLL3P       = 2U;
    periph.PLL3.PLL3Q       = 10U;  /* 480 MHz / 10 = 48 MHz */
    periph.PLL3.PLL3R       = 2U;
    periph.PLL3.PLL3RGE     = RCC_PLL3VCIRANGE_2;   /* 4 - 8 MHz input */
    periph.PLL3.PLL3VCOSEL  = RCC_PLL3VCOWIDE;
    periph.PLL3.PLL3FRACN   = 0U;
    periph.UsbClockSelection = RCC_USBCLKSOURCE_PLL3;

    if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

/**
  * @brief  PA11 / PA12 to AF10 (OTG2_FS), very high speed, no pull.
  */
static void usb_gpio_config(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG2_FS;
    HAL_GPIO_Init(GPIOA, &gpio);
}

int drv_usb_cdc_init(void)
{
    tusb_rhport_init_t rh =
    {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };

    s_ready = 0U;
    s_dtr   = 0U;

    if (usb_clock_config() != 0)
    {
        return -1;
    }

    usb_gpio_config();

    /* Power the embedded FS transceiver before touching the core. */
    HAL_PWREx_EnableUSBVoltageDetector();

    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
    __HAL_RCC_USB2_OTG_FS_ULPI_CLK_DISABLE();

    /* Below the SysTick priority so USB work never delays the tick, but above
     * the console UART so control transfers are answered in time. */
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);

    if (!tusb_rhport_init(BOARD_TUD_RHPORT, &rh))
    {
        return -1;
    }

    s_ready = 1U;

    return 0;
}

void drv_usb_cdc_task(void)
{
    if (s_ready != 0U)
    {
        tud_task();
    }
}

uint32_t drv_usb_cdc_write(const void *data, uint32_t len)
{
    uint32_t written;

    if ((s_ready == 0U) || (data == NULL) || (len == 0U))
    {
        return 0U;
    }

    if (!tud_cdc_connected())
    {
        return 0U;
    }

    written = tud_cdc_write(data, len);
    (void)tud_cdc_write_flush();

    return written;
}

uint32_t drv_usb_cdc_read(void *data, uint32_t len)
{
    if ((s_ready == 0U) || (data == NULL) || (len == 0U))
    {
        return 0U;
    }

    if (!tud_cdc_available())
    {
        return 0U;
    }

    return tud_cdc_read(data, len);
}

int drv_usb_cdc_connected(void)
{
    return ((s_ready != 0U) && (s_dtr != 0U)) ? 1 : 0;
}

/*----------------------------------------------------------------------------
 *  tinyusb callbacks
 *--------------------------------------------------------------------------*/

void tud_mount_cb(void)
{
    /* Enumerated, but the port may still be closed on the host side. */
}

void tud_umount_cb(void)
{
    s_dtr = 0U;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    s_dtr = 0U;
}

void tud_resume_cb(void)
{
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    (void)rts;

    s_dtr = dtr ? 1U : 0U;

    /* Flush whatever the host missed while the port was closed. */
    if (dtr)
    {
        tud_cdc_write_clear();
    }
}

/*----------------------------------------------------------------------------
 *  Interrupt
 *--------------------------------------------------------------------------*/

void OTG_FS_IRQHandler(void)
{
    tud_int_handler(BOARD_TUD_RHPORT);
}
