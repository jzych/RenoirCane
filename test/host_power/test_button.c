#include <assert.h>
#include <stdio.h>

#include "power_button.h"
#include "bluetooth_audio_policy.h"

int main(void) {
    power_button_t button;
    power_button_init(&button);
    assert(!power_button_confirm_press(&button, true));
    power_button_note_falling_edge(&button);
    assert(!power_button_confirm_press(&button, false));
    power_button_note_falling_edge(&button);
    assert(power_button_confirm_press(&button, true));
    assert(!power_button_confirm_press(&button, true));

    bt_audio_policy_t policy;
    bluetooth_audio_policy_init(&policy);
    assert(bluetooth_audio_policy_should_connect(&policy, 0));
    bluetooth_audio_policy_on_disconnected(&policy, 1000000);
    assert(!bluetooth_audio_policy_should_connect(&policy, 2999999));
    assert(bluetooth_audio_policy_should_connect(&policy, 3000000));
    assert(bluetooth_audio_policy_attempted_speaker(&policy) == BT_AUDIO_SPEAKER_SECONDARY);
    puts("PASS: button acceptance, consumed press, and 2-second reconnect boundary");
    return 0;
}
