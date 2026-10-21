/**
  ******************************************************************************
  * @file    board_io.c
  * @brief   LED + buzzer control and the LAN8720A PHY reset sequencing.
  *
  *  Init order matters here:
  *    POST_KERNEL prio 50 : Zephyr STM32 I2C driver
  *    POST_KERNEL prio 55 : this file releases ETH_RESET through the PCF8574
  *    POST_KERNEL prio 60 : Zephyr STM32 Ethernet driver probes the PHY
  *
  *  If the PHY were still held in reset when the MAC driver probes its PHY ID
  *  the interface would come up "dead" with no obvious error, so the release
  *  is deliberately pinned between the I2C and the Ethernet init priorities.
  ******************************************************************************
  */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include "board_io.h"

LOG_MODULE_REGISTER(board_io, LOG_LEVEL_INF);

/* PCF8574T on I2C2 (PH4 = SCL, PH5 = SDA), address A2=A1=A0=0 -> 0x20 */
#define PCF8574_ADDR        0x20U
#define PCF_BEEP_BIT        0U   /* P0: LOW = buzzer sounds */
#define PCF_ETH_RST_BIT     7U   /* P7: 1 = ETH_RESET asserted (active-low) */

#define PCF_STATE_RESET     0xFFU /* P7 = 1 -> LAN8720 held in reset     */
#define PCF_STATE_RUNNING   0x7FU /* P7 = 0 -> released, P0 = 1 -> quiet */

#define PHY_RESET_HOLD_MS   50U
#define PHY_RESET_SETTLE_MS 50U

/* I2C and Ethernet are both POST_KERNEL devices; 55 lands between the STM32
 * I2C driver (CONFIG_I2C_INIT_PRIORITY, default 50) and the Ethernet driver
 * (CONFIG_ETH_INIT_PRIORITY, default 60). */
#define BOARD_PHY_RESET_PRIO 55

static const struct device *const i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c2));
static const struct gpio_dt_spec led_user = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* Shadow copy of the expander output register. The PCF8574 has no internal
 * register file, every write replaces all eight pins, so the state must be
 * tracked here to avoid clobbering P7 (ETH reset) when toggling P0 (buzzer). */
static uint8_t pcf_state = PCF_STATE_RUNNING;

static bool led_on;
static bool beep_on;

static int pcf_write(uint8_t value)
{
	int rc = i2c_write(i2c_dev, &value, 1, PCF8574_ADDR);

	if (rc == 0) {
		pcf_state = value;
	}

	return rc;
}

static int phy_reset_release(void)
{
	int rc;

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C2 not ready, cannot release the LAN8720 reset");
		return -ENODEV;
	}

	/* Assert reset first so the PHY always starts from a known state... */
	rc = pcf_write(PCF_STATE_RESET);
	if (rc != 0) {
		LOG_ERR("PCF8574 write failed at reset assert (%d)", rc);
		return rc;
	}
	k_busy_wait(PHY_RESET_HOLD_MS * 1000U);

	/* ...then release it and let the PHY PLL settle. */
	rc = pcf_write(PCF_STATE_RUNNING);
	if (rc != 0) {
		LOG_ERR("PCF8574 write failed at reset release (%d)", rc);
		return rc;
	}
	k_busy_wait(PHY_RESET_SETTLE_MS * 1000U);

	LOG_INF("LAN8720 reset released (PCF8574 P7 = 0)");
	return 0;
}

SYS_INIT(phy_reset_release, POST_KERNEL, BOARD_PHY_RESET_PRIO);

int board_io_init(void)
{
	int rc;

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C2 device not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&led_user)) {
		LOG_ERR("LED GPIO not ready");
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&led_user, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		LOG_ERR("LED GPIO configure failed (%d)", rc);
		return rc;
	}

	led_on = false;
	beep_on = false;

	/* Make sure the buzzer is silent without disturbing P7. */
	rc = pcf_write((uint8_t)(pcf_state | BIT(PCF_BEEP_BIT)));
	if (rc != 0) {
		LOG_ERR("PCF8574 init write failed (%d)", rc);
		return rc;
	}

	return 0;
}

int board_led_set(bool on)
{
	int rc = gpio_pin_set_dt(&led_user, on ? 1 : 0);

	if (rc == 0) {
		led_on = on;
	}

	return rc;
}

bool board_led_get(void)
{
	return led_on;
}

int board_beep_set(bool on)
{
	uint8_t next = pcf_state;

	if (on) {
		next &= (uint8_t)~BIT(PCF_BEEP_BIT);
	} else {
		next |= (uint8_t)BIT(PCF_BEEP_BIT);
	}

	if (next == pcf_state) {
		beep_on = on;
		return 0;
	}

	int rc = pcf_write(next);

	if (rc == 0) {
		beep_on = on;
	}

	return rc;
}

bool board_beep_get(void)
{
	return beep_on;
}
