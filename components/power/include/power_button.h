#pragma once

#include <stdbool.h>

/* Task-local state; the GPIO ISR only sends a task notification. */
typedef struct {
    bool debounce_pending;
} power_button_t;

void power_button_init(power_button_t *button);
void power_button_note_falling_edge(power_button_t *button);
/* Called by the task after the debounce delay, with the current pin level. */
bool power_button_confirm_press(power_button_t *button, bool pin_is_low);
