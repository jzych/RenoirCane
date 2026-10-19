#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Stable OFF (GPIO high) requests sleep; ON (GPIO low) cancels the timer. */
typedef struct {
    bool off_pending;
    int64_t off_since_us;
} power_switch_debounce_t;

void power_switch_debounce_init(power_switch_debounce_t *state);
bool power_switch_should_sleep(power_switch_debounce_t *state, bool pin_is_high, int64_t now_us);
