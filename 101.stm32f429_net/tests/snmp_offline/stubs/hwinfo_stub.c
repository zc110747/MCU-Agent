/* hwinfo_stub.c — offline test implementation of the shared hwinfo API. */
#include "hwinfo.h"
#include <string.h>

static hwinfo_static_t  g_sta;
static hwinfo_dynamic_t g_dyn;
static uint8_t g_led, g_beep;

void hwinfo_init(void)
{
  g_sta.mcu   = "STM32F429IGT6";
  g_sta.clock = "180 MHz";
  strncpy(g_sta.ip,   "192.168.10.99", NETCFG_IP_LEN);
  strncpy(g_sta.mask, "255.255.255.0", NETCFG_IP_LEN);
  strncpy(g_sta.gw,   "192.168.10.1",  NETCFG_IP_LEN);
  strncpy(g_sta.mac,  "00:11:22:33:44:55", NETCFG_MAC_LEN);
  g_sta.freertos_tasks = 7;

  memset(&g_dyn, 0, sizeof(g_dyn));
  g_dyn.lux = 123; g_dyn.ps = 45; g_dyn.ir = 67;
  g_dyn.ax = 1.23f; g_dyn.ay = -0.50f; g_dyn.az = 9.81f;
  g_dyn.gx = 12.5f; g_dyn.gy = 0.0f; g_dyn.gz = -3.3f;
  g_dyn.mx = 25.0f; g_dyn.my = 10.0f; g_dyn.mz = 5.0f;
  g_dyn.sensor_valid = 1;
  g_dyn.led_on = 0; g_dyn.beep_on = 0;
  g_led = 0; g_beep = 0;
}

void hwinfo_static_copy(hwinfo_static_t *dst){ *dst = g_sta; }
void hwinfo_dynamic_copy(hwinfo_dynamic_t *dst){ *dst = g_dyn; }

void hwinfo_set_led(uint8_t on){ g_dyn.led_on = on?1:0; g_led = on?1:0; }
void hwinfo_set_beep(uint8_t on){ g_dyn.beep_on = on?1:0; g_beep = on?1:0; }

uint8_t hwinfo_test_led(void){ return g_led; }
uint8_t hwinfo_test_beep(void){ return g_beep; }
