#include <assert.h>
#include <stdio.h>

#include "power_button.h"
#include "bluetooth_audio_policy.h"

int main(void) {
    power_switch_debounce_t state;
    power_switch_debounce_init(&state);
    assert(!power_switch_should_sleep(&state, false, 0));
    assert(!power_switch_should_sleep(&state, true, 1000));
    assert(!power_switch_should_sleep(&state, true, 50999));
    assert(power_switch_should_sleep(&state, true, 51000));
    assert(!power_switch_should_sleep(&state, false, 52000));
    assert(!power_switch_should_sleep(&state, true, 60000));
    assert(!power_switch_should_sleep(&state, false, 80000));
    assert(!power_switch_should_sleep(&state, true, 90000));
    assert(!power_switch_should_sleep(&state, true, 139999));
    assert(power_switch_should_sleep(&state, true, 140000));

    bt_audio_policy_t policy;
    bluetooth_audio_policy_init(&policy);
    assert(bluetooth_audio_policy_should_connect(&policy, 0));
    bluetooth_audio_policy_on_disconnected(&policy, 1000000);
    assert(!bluetooth_audio_policy_should_connect(&policy, 2999999));
    assert(bluetooth_audio_policy_should_connect(&policy, 3000000));
    assert(bluetooth_audio_policy_attempted_speaker(&policy) == BT_AUDIO_SPEAKER_SECONDARY);
    puts("PASS: switch OFF debounce/ON cancellation and 2-second reconnect boundary");
    return 0;
}
