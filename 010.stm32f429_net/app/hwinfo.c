/**
  ******************************************************************************
  * @file    hwinfo.c
  * @brief   Periodic hardware collector + shared static/dynamic info store.
  *
  *   - hwinfo_task(): every HWINFO_PERIOD_MS, reads AP3216C/MPU9250 (under the
  *     shared I2C lock), fills a local dynamic struct, preserves the current
  *     led/beep control state, then copies the WHOLE struct into g_dyn inside
  *     one critical section (atomic, single memcpy).
  *   - hwinfo_static_copy()/hwinfo_dynamic_copy(): copy the WHOLE struct out
  *     inside one critical section. Readers never see a half-updated struct.
  *   - hwinfo_set_led()/hwinfo_set_beep(): update the control field under a
  *     critical section and drive the hardware outside it.
  *
  *   The single critical section around each whole-struct memcpy keeps the
  *   reader and the collector mutually exclusive for only a few instructions,
  *   so a web/telnet/snmp request never blocks the collector noticeably.
  ******************************************************************************
  */
#include "hwinfo.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_ap3216.h"
#include "bsp_mpu9250.h"
#include "bsp_led.h"
#include "bsp_pcf8574.h"
#include "bsp_i2c.h"     /* BSP_I2C_Recover() */
#include "web_serve.h"   /* web_i2c_lock / web_i2c_unlock (shared I2C bus) */
#include "netcfg.h"

/* ---- backing stores ---- */
static hwinfo_static_t  g_sta;
static hwinfo_dynamic_t g_dyn;

/* forward declaration (defined below, spawned from hwinfo_init) */
static void hwinfo_task(void *arg);

/* ------------------------------------------------------------------ */
/* init                                                               */
/* ------------------------------------------------------------------ */

void hwinfo_init(void)
{
  /* Static info is constant after init. */
  g_sta.mcu   = "STM32F429IGT6";
  g_sta.clock = "180 MHz";
  strncpy(g_sta.ip,   g_netcfg.ip,   NETCFG_IP_LEN);
  strncpy(g_sta.mask, g_netcfg.mask, NETCFG_IP_LEN);
  strncpy(g_sta.gw,   g_netcfg.gw,   NETCFG_IP_LEN);
  strncpy(g_sta.mac,  g_netcfg.mac,  NETCFG_MAC_LEN);
  g_sta.freertos_tasks = uxTaskGetNumberOfTasks();

  /* Dynamic defaults: sensors zero, IO off. */
  memset(&g_dyn, 0, sizeof(g_dyn));
  g_dyn.led_on  = 0;
  g_dyn.beep_on = 0;

  /* Collector task. Bumped to 384 because BSP_I2C_Recover() (called on a
   * read failure) dives through HAL_I2C_DeInit/Init which is stack-heavy. */
  xTaskCreate(hwinfo_task, "hwinfo", 384, NULL,
              tskIDLE_PRIORITY + 2, NULL);
}

/* ------------------------------------------------------------------ */
/* collector                                                          */
/* ------------------------------------------------------------------ */

void hwinfo_task(void *arg)
{
  (void)arg;

  for (;;)
  {
    ap3216c_data_t als;
    mpu9250_data_t imu;
    hwinfo_dynamic_t d;

    web_i2c_lock();
    int ok1 = bsp_ap3216c_read(&als);
    int ok2 = bsp_mpu9250_read(&imu);
    web_i2c_unlock();

    /* I2C bus can lock up (a slave holding SDA low after noise / a reset).
     * Without recovery every later read times out and we'd publish all-zero
     * sensor data forever. Recover once, then retry the failed read(s). */
    if (ok1 != 0 || ok2 != 0)
    {
      if (BSP_I2C_Recover() == 0)
      {
        g_dyn.i2c_recover++;                 /* observe auto-heal events */
        web_i2c_lock();
        if (ok1 != 0) ok1 = bsp_ap3216c_read(&als);
        if (ok2 != 0) ok2 = bsp_mpu9250_read(&imu);
        web_i2c_unlock();
      }
    }

    memset(&d, 0, sizeof(d));
    if (ok1 == 0)
    {
      d.lux = als.lux;
      d.ps  = als.ps;
      d.ir  = als.ir;
    }
    if (ok2 == 0)
    {
      d.ax = imu.ax; d.ay = imu.ay; d.az = imu.az;
      d.gx = imu.gx; d.gy = imu.gy; d.gz = imu.gz;
      d.mx = imu.mx; d.my = imu.my; d.mz = imu.mz;
    }
    d.sensor_valid = (ok1 == 0 && ok2 == 0) ? 1 : 0;

    /* Preserve running counters / control state that only the set_* paths
     * or the recovery path may change. */
    d.led_on      = g_dyn.led_on;
    d.beep_on     = g_dyn.beep_on;
    d.i2c_recover = g_dyn.i2c_recover;     /* carry over the recovery count */

    d.updated_ms = xTaskGetTickCount();

    /* Atomic publish: whole struct copied under one critical section. */
    taskENTER_CRITICAL();
    g_dyn = d;
    taskEXIT_CRITICAL();

    vTaskDelay(pdMS_TO_TICKS(HWINFO_PERIOD_MS));
  }
}

/* ------------------------------------------------------------------ */
/* reader / writer helpers                                            */
/* ------------------------------------------------------------------ */

void hwinfo_static_copy(hwinfo_static_t *dst)
{
  taskENTER_CRITICAL();
  *dst = g_sta;
  taskEXIT_CRITICAL();
}

void hwinfo_dynamic_copy(hwinfo_dynamic_t *dst)
{
  taskENTER_CRITICAL();
  *dst = g_dyn;
  taskEXIT_CRITICAL();
}

void hwinfo_set_led(uint8_t on)
{
  uint8_t v = on ? 1 : 0;

  /* Update control field atomically. */
  taskENTER_CRITICAL();
  g_dyn.led_on = v;
  taskEXIT_CRITICAL();

  /* Drive hardware OUTSIDE the critical section (I2C/HAL may block). */
  if (v) BSP_LED_On(1); else BSP_LED_Off(1);
}

void hwinfo_set_beep(uint8_t on)
{
  uint8_t v = on ? 1 : 0;

  taskENTER_CRITICAL();
  g_dyn.beep_on = v;
  taskEXIT_CRITICAL();

  if (v) BSP_BEEP_On(); else BSP_BEEP_Off();
}
