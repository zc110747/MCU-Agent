#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

/* =========================================================================
 *  Board / GPIO configuration
 *  ESP32-S3 N16R8 (e.g. ESP32-S3-DevKitC-1 or generic N16R8 module)
 *  Change the values below to match YOUR board's schematic.
 * ======================================================================= */

/* LED_PIN: GPIO connected to the user LED.
 *   Different boards wire the user LED to different GPIOs:
 *     - 2   : many generic ESP32-S3 modules expose a user LED on GPIO2
 *     - 48  : ESP32-S3-DevKitC-1 uses a WS2812 *RGB* LED on GPIO48.
 *             That is NOT a plain digital pin -> do NOT use 48 here.
 *     - any free GPIO wired to an external LED + series resistor
 *   -> Edit to match your hardware. A logic-high turns the LED ON below.
 */
#ifndef LED_PIN
#define LED_PIN             2
#endif
#define LED_ACTIVE_HIGH     true    // false if your LED is active-low

/* BUTTON_PIN: GPIO connected to a user button.
 *   - Many ESP32-S3 boards expose the BOOT button on GPIO0 (active-low).
 *   - If your board has no user button, wire a tactile switch between this
 *     GPIO and GND (the driver enables the internal pull-up below).
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
