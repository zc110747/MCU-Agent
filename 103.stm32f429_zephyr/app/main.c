/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F429IGT6 + Zephyr web server demo.
  *
  *  Boot sequence
  *    1. board_io_init()  - configure the LEDs, silence the buzzer.
  *                          (The LAN8720 reset has already been released by a
  *                          SYS_INIT hook before the Ethernet driver probed
  *                          the PHY -- see app/board_io.c.)
  *    2. wait for the network interface to come up (static IPv4 from
  *       prj.conf: 192.168.10.8/24, gw 192.168.10.1).
  *    3. web_server_start() - HTTP server thread on port 80.
  *    4. heartbeat: PB1 blinks every 500 ms, independent of the web UI.
  ******************************************************************************
  */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>

#include "board_io.h"
#include "sdram.h"
#include "web_server.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define HEARTBEAT_PERIOD_MS 500
#define NET_UP_TIMEOUT_MS   15000

static const struct gpio_dt_spec led_hb = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static bool wait_for_network(void)
{
	struct net_if *iface = net_if_get_default();
	int32_t waited = 0;

	while (waited < NET_UP_TIMEOUT_MS) {
		if (iface != NULL && net_if_is_up(iface)) {
			return true;
		}
		k_msleep(100);
		waited += 100;
	}

	return false;
}

int main(void)
{
	int rc;

	LOG_INF("STM32F429IGT6 Zephyr web server demo");
	LOG_INF("Zephyr %s, board %s", KERNEL_VERSION_STRING, CONFIG_BOARD);

	rc = board_io_init();
	if (rc != 0) {
		LOG_ERR("board_io_init failed (%d)", rc);
	}

	/* Bring up the external SDRAM before anything might want a big buffer.
	 * The controller itself is initialised by the FMC driver; this call only
	 * self-tests the chip and publishes the application heap. */
	rc = sdram_init();
	if (rc != 0) {
		LOG_WRN("SDRAM unavailable (%d), using internal SRAM only", rc);
	}

	if (!gpio_is_ready_dt(&led_hb)) {
		LOG_ERR("heartbeat LED GPIO not ready");
	} else {
		(void)gpio_pin_configure_dt(&led_hb, GPIO_OUTPUT_INACTIVE);
	}

	if (wait_for_network()) {
		LOG_INF("network up: %s/%s gw %s",
			CONFIG_NET_CONFIG_MY_IPV4_ADDR,
			CONFIG_NET_CONFIG_MY_IPV4_NETMASK,
			CONFIG_NET_CONFIG_MY_IPV4_GW);
	} else {
		LOG_WRN("network interface did not come up, starting web server anyway");
	}

	rc = web_server_start();
	if (rc != 0) {
		LOG_ERR("web_server_start failed (%d)", rc);
	} else {
		LOG_INF("open http://%s/ in a browser", CONFIG_NET_CONFIG_MY_IPV4_ADDR);
	}

	for (;;) {
		if (gpio_is_ready_dt(&led_hb)) {
			(void)gpio_pin_toggle_dt(&led_hb);
		}
		k_msleep(HEARTBEAT_PERIOD_MS);
	}

	return 0;
}
