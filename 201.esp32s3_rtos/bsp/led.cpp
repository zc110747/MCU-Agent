#include "bsp/led.h"

/* LED driver.
 *
 * The user LED on this board is a WS2812B RGB LED on GPIO48 (single-wire
 * 800 kHz protocol, GRB byte order). It is NOT a plain digital pin, so it is
 * driven through the Adafruit_NeoPixel library (installed via
 * `arduino-cli lib install "Adafruit NeoPixel"`), which uses the ESP32 RMT
 * peripheral for cycle-accurate timing.
 *
 * The driver is the single owner of the LED: all on/off/toggle requests from
 * tasks / uart are funneled through these functions. `led_set(bool)` maps
 * "on" -> a fixed GRB color (LED_ON_*) and "off" -> black, so the existing
 * boolean task / UART interface keeps working unchanged. */

#if LED_IS_WS2812

#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel s_pixels(1, LED_PIN, NEO_GRB + NEO_KHZ800);
static bool              s_led_state = false;   // current logical state (true = ON)

void led_init(void) {
    s_pixels.begin();
    s_pixels.setBrightness(LED_WS2812_BRIGHTNESS);
    s_pixels.clear();          // all off
    s_pixels.show();
    s_led_state = false;
}

void led_set(bool on) {
    s_led_state = on;
    if (on) {
        s_pixels.setPixelColor(0, s_pixels.Color(LED_ON_R, LED_ON_G, LED_ON_B));
    } else {
        s_pixels.setPixelColor(0, 0, 0, 0);   // black = off
    }
    s_pixels.show();
}

void led_toggle(void) {
    led_set(!s_led_state);
}

#else  /* plain digital GPIO LED (external LED cases) */

static bool s_led_state = false;

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

#endif /* LED_IS_WS2812 */
