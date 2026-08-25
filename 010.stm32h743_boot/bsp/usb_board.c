/* ---------------------------------------------------------------------------
 * USB board support - STM32H743ZIT6, USB2_OTG_FS on PA11/PA12 (internal FS PHY)
 *
 * The board is wired to the embedded FS PHY (USB_OTG_FS). TinyUSB's DWC2 port
 * handles the core register programming and FIFO/endpoint scheduling; this file
 * only does what the stack expects the board to do first: enable the GPIOs,
 * the peripheral clock, the USB voltage detector, the SOF-based HSI48 CRS trim,
 * and the NVIC wiring.
 * -------------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"
#include "tusb.h"

/* Some HAL header revisions only expose one of the OTG spellings; pin it down. */
#ifndef GPIO_AF10_OTG_ANY
#define GPIO_AF10_OTG_ANY  ((uint8_t)0x0A)
#endif

/* ---------------------------------------------------------------------------
 * Clock Recovery System: continuously trims HSI48 against the host's SOF so the
 * 48 MHz USB clock keeps the 0.25% accuracy full-speed USB requires, with no
 * crystal involved.
 * -------------------------------------------------------------------------*/
static void usb_crs_config(void)
{
    RCC_CRSInitTypeDef crs = {0};

    __HAL_RCC_CRS_CLK_ENABLE();

    crs.Prescaler            = RCC_CRS_SYNC_DIV1;
    crs.Source              = RCC_CRS_SYNC_SOURCE_USB2;   /* OTG_FS generates SOF */
    crs.Polarity            = RCC_CRS_SYNC_POLARITY_RISING;
    crs.ReloadValue         = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);
    crs.ErrorLimitValue     = RCC_CRS_ERRORLIMIT_DEFAULT;
    crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;

    HAL_RCCEx_CRSConfig(&crs);
}

void BSP_USB_Init(void)
{
    GPIO_InitTypeDef g = {0};

    /* Enable HSI48 + CRS so the 48 MHz USB clock is available and accurate. */
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    osc.HSI48State     = RCC_HSI48_ON;
    HAL_RCC_OscConfig(&osc);

    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_USB;
    pclk.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
    HAL_RCCEx_PeriphCLKConfig(&pclk);

    usb_crs_config();

    /* PA11 = DM, PA12 = DP, alternate function 10 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF10_OTG_ANY;
    HAL_GPIO_Init(GPIOA, &g);

    /* Tell the USB transceiver its dedicated 3.3 V rail is valid. */
    HAL_PWREx_EnableUSBVoltageDetector();

    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();

    /* Leave room below the system handlers; the stack does its own critical
     * sections, so do not let the USB IRQ preempt anything that must not wait. */
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}
