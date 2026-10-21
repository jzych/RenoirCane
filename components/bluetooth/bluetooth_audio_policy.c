#include "bluetooth_audio_policy.h"

#include "app_config.h"

void bluetooth_audio_policy_init(bt_audio_policy_t *policy) {
    policy->state = BT_AUDIO_DISABLED;
    policy->retry_at_us = 0;
    policy->attempt_outstanding = false;
    policy->attempted_speaker = BT_AUDIO_SPEAKER_PRIMARY;
    policy->next_speaker = BT_AUDIO_SPEAKER_PRIMARY;
    policy->reconnect_requested = false;
    policy->media_streaming = false;
    policy->media_action_pending = BT_AUDIO_MEDIA_ACTION_NONE;
    policy->source_ready = false;
}

bool bluetooth_audio_policy_should_connect(bt_audio_policy_t *policy, int64_t now_us) {
    if (policy->state == BT_AUDIO_ERROR || policy->attempt_outstanding ||
        policy->state == BT_AUDIO_CONNECTED || policy->state == BT_AUDIO_STREAMING ||
        now_us < policy->retry_at_us) {
        return false;
    }

    policy->state = BT_AUDIO_CONNECTING;
    policy->attempt_outstanding = true;
    policy->attempted_speaker = policy->next_speaker;
    policy->next_speaker = policy->next_speaker == BT_AUDIO_SPEAKER_PRIMARY
                               ? BT_AUDIO_SPEAKER_SECONDARY
                               : BT_AUDIO_SPEAKER_PRIMARY;
    return true;
}

bt_audio_speaker_t bluetooth_audio_policy_attempted_speaker(const bt_audio_policy_t *policy) {
    return policy->attempted_speaker;
}

void bluetooth_audio_policy_on_connection_request_failed(bt_audio_policy_t *policy, int64_t now_us) {
    if (!policy->reconnect_requested) {
        policy->next_speaker = policy->attempted_speaker;
    }
    bluetooth_audio_policy_on_disconnected(policy, now_us);
}

bool bluetooth_audio_policy_request_reconnect(bt_audio_policy_t *policy) {
    if (policy->state == BT_AUDIO_ERROR) return false;
    bool disconnect = policy->state == BT_AUDIO_CONNECTED || policy->state == BT_AUDIO_STREAMING;
    if (policy->state == BT_AUDIO_CONNECTING || disconnect) {
        /* ESP-IDF drops A2DP disconnect requests while the connection is still opening.  Preserve
           this request until the connection callback, which can disconnect the established link. */
        policy->reconnect_requested = true;
    } else {
        bluetooth_audio_policy_init(policy);
    }
    return disconnect;
}

bool bluetooth_audio_policy_should_keep_media_started(const bt_audio_policy_t *policy) {
    return policy->state == BT_AUDIO_CONNECTED || policy->state == BT_AUDIO_STREAMING;
}

bt_audio_media_action_t bluetooth_audio_policy_next_media_action(bt_audio_policy_t *policy) {
    if (policy->state != BT_AUDIO_CONNECTED || policy->media_streaming ||
        policy->media_action_pending != BT_AUDIO_MEDIA_ACTION_NONE) {
        return BT_AUDIO_MEDIA_ACTION_NONE;
    }

    policy->media_action_pending = policy->source_ready ? BT_AUDIO_MEDIA_ACTION_START
                                                         : BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY;
    return policy->media_action_pending;
}

bool bluetooth_audio_policy_on_source_ready(bt_audio_policy_t *policy, bool ready) {
    if (policy->state != BT_AUDIO_CONNECTED ||
        policy->media_action_pending != BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY) {
        return false;
    }
    policy->media_action_pending = BT_AUDIO_MEDIA_ACTION_NONE;
    policy->source_ready = ready;
    return true;
}

bool bluetooth_audio_policy_on_media_start_result(bt_audio_policy_t *policy, bool started) {
    if (policy->state != BT_AUDIO_CONNECTED || policy->media_action_pending != BT_AUDIO_MEDIA_ACTION_START) {
        return false;
    }
    policy->media_action_pending = BT_AUDIO_MEDIA_ACTION_NONE;
    policy->media_streaming = started;
    if (!started) {
        policy->source_ready = false;
    }
    return true;
}

bool bluetooth_audio_policy_on_media_suspended(bt_audio_policy_t *policy) {
    if (policy->state != BT_AUDIO_CONNECTED && policy->state != BT_AUDIO_STREAMING) {
        return false;
    }
    policy->media_streaming = false;
    policy->media_action_pending = BT_AUDIO_MEDIA_ACTION_NONE;
    policy->source_ready = false;
    return true;
}

bool bluetooth_audio_policy_on_connected(bt_audio_policy_t *policy) {
    if (policy->state == BT_AUDIO_ERROR) return false;
    bool reconnect_requested = policy->reconnect_requested;
    policy->state = BT_AUDIO_CONNECTED;
    policy->attempt_outstanding = false;
    policy->media_streaming = false;
    policy->media_action_pending = BT_AUDIO_MEDIA_ACTION_NONE;
    policy->source_ready = false;
    return reconnect_requested;
}

void bluetooth_audio_policy_on_disconnected(bt_audio_policy_t *policy, int64_t now_us) {
    if (policy->state == BT_AUDIO_ERROR) return;
    if (policy->reconnect_requested) {
        bluetooth_audio_policy_init(policy);
        return;
    }
    policy->state = BT_AUDIO_DISABLED;
    policy->retry_at_us = now_us + (int64_t)APP_BT_RECONNECT_MS * 1000;
    policy->attempt_outstanding = false;
    policy->media_streaming = false;
    policy->media_action_pending = BT_AUDIO_MEDIA_ACTION_NONE;
    policy->source_ready = false;
}

void bluetooth_audio_policy_on_recoverable_error(bt_audio_policy_t *policy, int64_t now_us) {
    if (policy->state == BT_AUDIO_ERROR) return;
    policy->reconnect_requested = false;
    bluetooth_audio_policy_on_disconnected(policy, now_us);
}
