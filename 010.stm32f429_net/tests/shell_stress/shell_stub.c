/* PC stub implementations for shell.c dependencies.
 * Headers (hwinfo.h, bsp_uart.h, netcfg.h, bsp_led.h, bsp_pcf8574.h, FreeRTOS.h)
 * come from the inc/ stub dir; here we only provide the function bodies so the
 * REAL app/shell.c parsing logic can be linked natively and stress-tested.
 */
#include "hwinfo.h"
#include "bsp_uart.h"
#include "netcfg.h"
#include "bsp_led.h"
#include "bsp_pcf8574.h"
#include <string.h>

static uint8_t g_led = 0, g_beep = 0;

void hwinfo_init(void) {}
void hwinfo_static_copy(hwinfo_static_t *d) {
  static const hwinfo_static_t s = {
    "STM32F429IGT6", "180 MHz",
    "192.168.10.99", "255.255.255.0", "192.168.10.1", "00:80:E1:42:10:99",
    7
  };
  *d = s;
}
void hwinfo_dynamic_copy(hwinfo_dynamic_t *d) {
  memset(d, 0, sizeof(*d));
  d->lux = 123; d->ps = 45; d->ir = 678;
  d->ax = 1.2f; d->ay = -0.3f; d->az = 9.8f;
  d->gx = 12.5f; d->gy = -3.0f; d->gz = 0.1f;
  d->mx = 22.0f; d->my = 11.0f; d->mz = -5.0f;
  d->sensor_valid = 1;
  d->led_on = g_led; d->beep_on = g_beep; d->updated_ms = 12345;
}
void hwinfo_set_led(uint8_t v)  { g_led  = v ? 1 : 0; }
void hwinfo_set_beep(uint8_t v) { g_beep = v ? 1 : 0; }

int uart_write(const uint8_t *data, int len) { (void)data; return len; }
int uart_puts(const char *s) { (void)s; return 0; }
int uart_getc(uint8_t *c) { (void)c; return 0; }

void BSP_LED_On(int n)  { (void)n; }
void BSP_LED_Off(int n) { (void)n; }
void BSP_PCF8574_WriteBit(uint8_t a, uint8_t b, uint8_t v) { (void)a;(void)b;(void)v; }
