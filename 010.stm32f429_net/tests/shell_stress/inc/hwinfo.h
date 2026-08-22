#ifndef INC_HWINFO_H
#define INC_HWINFO_H
#include <stdint.h>
#include "netcfg.h"

typedef struct {
  const char *mcu;
  const char *clock;
  char ip[NETCFG_IP_LEN];
  char mask[NETCFG_IP_LEN];
  char gw[NETCFG_IP_LEN];
  char mac[NETCFG_MAC_LEN];
  uint32_t freertos_tasks;
} hwinfo_static_t;

typedef struct {
  uint16_t lux, ps, ir;
  float ax, ay, az, gx, gy, gz, mx, my, mz;
  uint8_t sensor_valid;
  uint8_t led_on;
  uint8_t beep_on;
  uint32_t updated_ms;
} hwinfo_dynamic_t;

#define HWINFO_PERIOD_MS 200

void hwinfo_init(void);
void hwinfo_static_copy(hwinfo_static_t *dst);
void hwinfo_dynamic_copy(hwinfo_dynamic_t *dst);
void hwinfo_set_led(uint8_t v);
void hwinfo_set_beep(uint8_t v);
#endif
