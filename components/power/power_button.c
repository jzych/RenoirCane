#include "power_button.h"

#include "app_config.h"

void power_switch_debounce_init(power_switch_debounce_t *state) {
    state->off_pending = false;
    state->off_since_us = 0;
}

bool power_switch_should_sleep(power_switch_debounce_t *state, bool pin_is_high, int64_t now_us) {
    if (!pin_is_high) {
        state->off_pending = false;
        return false;
    }
    if (!state->off_pending) {
        state->off_pending = true;
        state->off_since_us = now_us;
    }
    return now_us - state->off_since_us >= (int64_t)APP_SLEEP_BUTTON_DEBOUNCE_MS * 1000;
}
