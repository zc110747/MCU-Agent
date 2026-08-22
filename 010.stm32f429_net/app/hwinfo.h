/**
  ******************************************************************************
  * @file    hwinfo.h
  * @brief   Shared system information (static + dynamic) for multi-interface
  *          access (web / telnet / snmp).
  *
  *          - Static info (MCU, clock, IP, MAC, FreeRTOS task count) is set
  *            once at init and does not change at runtime.
  *          - Dynamic info (sensors, LED/BEEP state) is refreshed periodically
  *            (200 ms) by hwinfo_task and read by any interface.
  *
  *          Concurrency: the whole struct is copied under a single critical
  *          section (reader memcpy OUT, collector memcpy IN). Each individual
  *          field assignment is therefore atomic and a reader never observes a
  *          half-updated struct. Readers never block the collector for more
  *          than one memcpy.
  ******************************************************************************
  */
#ifndef __HWINFO_H__
#define __HWINFO_H__

#include <stdint.h>
#include "netcfg.h"

/* Refresh period of the dynamic collector (ms). */
#define HWINFO_PERIOD_MS   200

/* ---- Static info: set once at init, then read-only ---- */
typedef struct {
  const char *mcu;            /* "STM32F429IGT6" */
  const char *clock;          /* "180 MHz" */
  char ip[NETCFG_IP_LEN];     /* from g_netcfg */
  char mask[NETCFG_IP_LEN];
  char gw[NETCFG_IP_LEN];
  char mac[NETCFG_MAC_LEN];
  uint32_t freertos_tasks;    /* uxTaskGetNumberOfTasks() at init */
} hwinfo_static_t;

/* ---- Dynamic info: refreshed every HWINFO_PERIOD_MS ---- */
typedef struct {
  /* AP3216C ambient light / proximity / IR */
  uint16_t lux;
  uint16_t ps;
  uint16_t ir;
  /* MPU9250 9-axis (float, 1 decimal place on wire) */
  float ax, ay, az;           /* accelerometer (g) */
  float gx, gy, gz;           /* gyroscope (dps) */
  float mx, my, mz;           /* magnetometer (uT) */
  uint8_t sensor_valid;       /* 0 = last read failed */
  /* IO state driven by web/telnet/snmp */
  uint8_t led_on;             /* web-controlled LED (PB0, non-heartbeat) */
  uint8_t beep_on;            /* BEEP (PCF8574 P0, low=sound) */
  /* collector tick stamp (ms) for freshness checks */
  uint32_t updated_ms;
} hwinfo_dynamic_t;

/* Initialize static info + start the collector task. Call after
 * vTaskStartScheduler(). */
void hwinfo_init(void);

/* Copy the whole static struct out (atomic, under critical section). */
void hwinfo_static_copy(hwinfo_static_t *dst);

/* Copy the whole dynamic struct out (atomic, under critical section). */
void hwinfo_dynamic_copy(hwinfo_dynamic_t *dst);

/* Control entry points shared by all interfaces. Each updates one field
 * under a critical section and drives the hardware. */
void hwinfo_set_led(uint8_t on);
void hwinfo_set_beep(uint8_t on);

#endif /* __HWINFO_H__ */
