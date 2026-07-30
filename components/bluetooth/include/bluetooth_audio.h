#pragma once

#include <stdbool.h>

#include "bluetooth_audio_policy.h"

typedef enum {
    BT_AUDIO_PLAY_STARTED,
    BT_AUDIO_PLAY_DISCONNECTED,
    BT_AUDIO_PLAY_BUSY,
    BT_AUDIO_PLAY_ERROR,
} bt_audio_play_result_t;

typedef void (*bt_audio_state_callback_t)(bt_audio_state_t state);

bool bluetooth_audio_init(bt_audio_state_callback_t callback);
void bluetooth_audio_start(void);
bt_audio_play_result_t bluetooth_audio_start_impact(void);
void bluetooth_audio_request_reconnect(void);
bool bluetooth_audio_is_connected(void);
bt_audio_state_t bluetooth_audio_get_state(void);
