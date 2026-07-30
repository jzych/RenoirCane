#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BT_AUDIO_DISABLED,
    BT_AUDIO_CONNECTING,
    BT_AUDIO_CONNECTED,
    BT_AUDIO_STREAMING,
    BT_AUDIO_ERROR,
} bt_audio_state_t;

typedef struct {
    bt_audio_state_t state;
    int64_t retry_at_us;
    bool attempt_outstanding;
} bt_audio_policy_t;

void bluetooth_audio_policy_init(bt_audio_policy_t *policy);
bool bluetooth_audio_policy_should_connect(bt_audio_policy_t *policy, int64_t now_us);
void bluetooth_audio_policy_on_connected(bt_audio_policy_t *policy);
void bluetooth_audio_policy_on_disconnected(bt_audio_policy_t *policy, int64_t now_us);
