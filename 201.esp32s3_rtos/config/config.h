#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

/* =========================================================================
 *  Board / GPIO configuration
 *  ESP32-S3-COREBOARD V1.4 (verified against ESP32-S3-SCH-V1.4.pdf)
 *
 *  User LED: a WS2812B RGB LED wired to GPIO48 (single-wire 800 kHz protocol,
 *  GRB order). This is NOT a plain digital pin -> it is driven by the WS2812
 *  driver in bsp/led.cpp (Adafruit_NeoPixel, installed via
 *  `arduino-cli lib install "Adafruit NeoPixel"`).
 *
 *  Board status LEDs (hardware-driven, not software-controllable):
 *    - PWRLED-RED : power indicator (+5V -> R6 1k -> LED -> GND, always on)
 *    - TXLED2     : UART TX activity (U0TXD/GPIO43 -> R4 1k -> LED -> GND)
 *    - RXLED2     : UART RX activity (U0RXD/GPIO44 -> R1 1k -> LED -> GND)
 * ======================================================================= */

/* LED configuration.
 *   LED_PIN      : GPIO the WS2812B data line is connected to (48 on this board)
 *   LED_IS_WS2812: 1 = WS2812B single-wire RGB (use WS2812 driver)
 *                  0 = plain digital GPIO LED (use digitalWrite driver)
 *   LED_WS2812_BRIGHTNESS : 0..255 global dimming (kept low to avoid glare)
 *   LED_ON_*:    GRB color applied when the LED is logically "on"
 */
#ifndef LED_PIN
#define LED_PIN             48      /* WS2812B data line (COREBOARD V1.4) */
#endif
#define HAS_USER_LED        (LED_PIN >= 0)
#define LED_IS_WS2812       1

#if LED_IS_WS2812
#define LED_WS2812_BRIGHTNESS  40   /* 0..255, dimmed to avoid glare */
/* Color shown when the LED is "on" (GRB order for WS2812B). */
#define LED_ON_R              0
#define LED_ON_G              64
#define LED_ON_B              32
#else
/* Plain digital LED polarity (unused when LED_IS_WS2812 == 1). */
#define LED_ACTIVE_HIGH       true    /* false if your LED is active-low */
#endif

/* BUTTON_PIN: GPIO connected to a user button.
 *   COREBOARD V1.4: BOOT button on GPIO0 (active-low, R5 10k pull-up).
 *   Verified against schematic: BOOT -> GPIO0, R5(10k) to VDD33.
 *   If your board has no user button, wire a tactile switch between this
 *   GPIO and GND (the driver enables the internal pull-up below).
 */
#ifndef BUTTON_PIN
#define BUTTON_PIN          0
#endif
#define BUTTON_ACTIVE_LOW   true    // BOOT button is active-low; false for active-high

/* =========================================================================
 *  UART (used by Task_UART and the startup banner)
 *  We use the Arduino 'Serial' object which maps to UART0 (TXD0/RXD0),
 *  already wired to the CH343 by the board definition. Leave pins at -1
 *  to keep the board default routing.
 * ======================================================================= */
#define UART_BAUD           115200
#define UART_RX_PIN         -1      // -1 = board default (RXD0)
#define UART_TX_PIN         -1      // -1 = board default (TXD0)

/* =========================================================================
 *  Task timing (milliseconds). All tasks use vTaskDelayUntil(), never delay().
 * ======================================================================= */
#define LED_PERIOD_MS       500
#define BUTTON_POLL_MS      20
#define MONITOR_PERIOD_MS   1000
#define UART_POLL_MS        50

/* =========================================================================
 *  FreeRTOS object sizing
 * ======================================================================= */
#define APP_EVENT_QUEUE_LEN   16    // events buffered between producers/consumers
#define QUEUE_SEND_TIMEOUT_MS 50    // max block time when sending an event

/* =========================================================================
 *  Task priorities (higher number = higher priority)
 *    UART/Button handle interactive I/O -> higher priority so user input
 *    is never starved. LED is a trivial heartbeat -> medium. Monitor only
 *    prints statistics, never blocks real work -> lowest priority.
 * ======================================================================= */
#define PRIO_UART            3
#define PRIO_BUTTON          3
#define PRIO_LED             2
#define PRIO_MONITOR         1

/* =========================================================================
 *  Core affinity
 *  ESP32-S3 is dual-core. Arduino/ESP-IDF keep network & system tasks on
 *  Core 0; we pin our application tasks to Core 1 so they are isolated
 *  from the system stack. (Use tskNO_AFFINITY to let the scheduler choose.)
 * ======================================================================= */
#define CORE_APP_TASKS       1
#define CORE_SYSTEM          0

#endif /* CONFIG_H */
