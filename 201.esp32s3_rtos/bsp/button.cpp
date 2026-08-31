#include "bsp/button.h"

/* Debounce: require N consecutive stable samples (~N * BUTTON_POLL_MS) before
 * accepting a state change. Edge-detect so each physical press reports
 * exactly one "pressed" event. Called periodically by Task_Button. */

static const int DEBOUNCE_SAMPLES = 3;   // ~60 ms at 20 ms poll period

static bool s_stable       = false;
static bool s_reported     = false;
static int  s_debounce_cnt = 0;

static inline bool raw_pressed(void) {
    int lvl = digitalRead(BUTTON_PIN);
    return (lvl == (BUTTON_ACTIVE_LOW ? LOW : HIGH));
}

void button_init(void) {
    /* BOOT-style buttons are active-low -> internal pull-up. */
    pinMode(BUTTON_PIN, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    s_stable       = raw_pressed();
    s_reported     = false;
    s_debounce_cnt = 0;
}

bool button_poll_pressed(void) {
    bool raw = raw_pressed();

    if (raw == s_stable) {
        s_debounce_cnt = 0;
    } else {
        s_debounce_cnt++;
        if (s_debounce_cnt >= DEBOUNCE_SAMPLES) {
            s_stable       = raw;
            s_debounce_cnt = 0;
        }
    }

    if (s_stable && !s_reported) {
        s_reported = true;
        return true;
    }
    if (!s_stable) {
        s_reported = false;   // re-arm for the next press
    }
    return false;
}
