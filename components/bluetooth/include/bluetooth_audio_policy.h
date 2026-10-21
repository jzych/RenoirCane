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

typedef enum {
    BT_AUDIO_MEDIA_ACTION_NONE,
    BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY,
    BT_AUDIO_MEDIA_ACTION_START,
} bt_audio_media_action_t;

typedef enum {
    BT_AUDIO_SPEAKER_PRIMARY,
    BT_AUDIO_SPEAKER_SECONDARY,
} bt_audio_speaker_t;

typedef struct {
    bt_audio_state_t state;
    int64_t retry_at_us;
    bool attempt_outstanding;
    bt_audio_speaker_t attempted_speaker;
    bt_audio_speaker_t next_speaker;
    bool reconnect_requested;
    bool media_streaming;
    bt_audio_media_action_t media_action_pending;
    bool source_ready;
} bt_audio_policy_t;

void bluetooth_audio_policy_init(bt_audio_policy_t *policy);
bool bluetooth_audio_policy_should_connect(bt_audio_policy_t *policy, int64_t now_us);
bt_audio_speaker_t bluetooth_audio_policy_attempted_speaker(const bt_audio_policy_t *policy);
void bluetooth_audio_policy_on_connection_request_failed(bt_audio_policy_t *policy, int64_t now_us);
bool bluetooth_audio_policy_request_reconnect(bt_audio_policy_t *policy);
bool bluetooth_audio_policy_should_keep_media_started(const bt_audio_policy_t *policy);
bt_audio_media_action_t bluetooth_audio_policy_next_media_action(bt_audio_policy_t *policy);
bool bluetooth_audio_policy_on_source_ready(bt_audio_policy_t *policy, bool ready);
bool bluetooth_audio_policy_on_media_start_result(bt_audio_policy_t *policy, bool started);
bool bluetooth_audio_policy_on_media_suspended(bt_audio_policy_t *policy);
/* Returns true when a manual reconnect was requested while the link was opening.  The caller must
   disconnect the now-established A2DP link so the normal disconnected path can reconnect. */
bool bluetooth_audio_policy_on_connected(bt_audio_policy_t *policy);
void bluetooth_audio_policy_on_disconnected(bt_audio_policy_t *policy, int64_t now_us);
/* Drop stale media/manual-reconnect state and wait the normal retry interval.
   Speaker alternation is preserved; fatal BT_AUDIO_ERROR remains terminal. */
void bluetooth_audio_policy_on_recoverable_error(bt_audio_policy_t *policy, int64_t now_us);
