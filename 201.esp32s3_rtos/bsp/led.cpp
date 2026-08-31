#include "bsp/led.h"

/* LED driver. Single owner of the LED GPIO; all on/off/toggle requests from
 * tasks/uart are funneled through these functions so the pin state is never
 * toggled from multiple places. */

static bool s_led_state = false;   // current logical state (true = ON)

void led_init(void) {
    pinMode(LED_PIN, OUTPUT);
    s_led_state = false;
    digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? LOW : HIGH);
}

void led_set(bool on) {
    s_led_state = on;
    digitalWrite(LED_PIN, (on == LED_ACTIVE_HIGH) ? HIGH : LOW);
}

void led_toggle(void) {
    led_set(!s_led_state);
}
