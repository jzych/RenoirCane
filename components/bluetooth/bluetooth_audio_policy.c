#include "bluetooth_audio_policy.h"

#include "app_config.h"

void bluetooth_audio_policy_init(bt_audio_policy_t *policy) {
    policy->state = BT_AUDIO_DISABLED;
    policy->retry_at_us = 0;
    policy->attempt_outstanding = false;
}

bool bluetooth_audio_policy_should_connect(bt_audio_policy_t *policy, int64_t now_us) {
    if (policy->attempt_outstanding || policy->state == BT_AUDIO_CONNECTED || policy->state == BT_AUDIO_STREAMING ||
        now_us < policy->retry_at_us) {
        return false;
    }

    policy->state = BT_AUDIO_CONNECTING;
    policy->attempt_outstanding = true;
    return true;
}

void bluetooth_audio_policy_on_connected(bt_audio_policy_t *policy) {
    policy->state = BT_AUDIO_CONNECTED;
    policy->attempt_outstanding = false;
}

void bluetooth_audio_policy_on_disconnected(bt_audio_policy_t *policy, int64_t now_us) {
    policy->state = BT_AUDIO_DISABLED;
    policy->retry_at_us = now_us + (int64_t)APP_BT_RECONNECT_MS * 1000;
    policy->attempt_outstanding = false;
}
