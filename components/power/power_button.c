#include "power_button.h"

void power_button_init(power_button_t *button) {
    button->debounce_pending = false;
}

void power_button_note_falling_edge(power_button_t *button) {
    button->debounce_pending = true;
}

bool power_button_confirm_press(power_button_t *button, bool pin_is_low) {
    bool accepted = button->debounce_pending && pin_is_low;
    button->debounce_pending = false;
    return accepted;
}
