#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "accelerometer.h"
#include "bluetooth_audio.h"
typedef struct { app_impact_axis_t axis; int16_t threshold_raw; uint32_t refractory_ms; } impact_config_t;
typedef struct { int64_t timestamp_us; accel_sample_t sample; uint32_t axis_abs_raw; } impact_event_t;
typedef enum {
    IMPACT_CANDIDATE_BELOW_THRESHOLD,
    IMPACT_CANDIDATE_REFRACTORY,
    IMPACT_CANDIDATE_PROCESSED,
    IMPACT_CANDIDATE_AUDIO_BUSY,
    IMPACT_CANDIDATE_AUDIO_UNAVAILABLE,
    IMPACT_CANDIDATE_NO_TAP,
} impact_candidate_result_t;

/* Both modes use the original normal-mode order: confirm/clear a pending tap,
   then read its sample. With no pending tap, acquire a diagnostic/graph sample.
   tap_confirmed is meaningful even when sample acquisition fails. */
bool impact_read_sample(bool tap_pending, accel_sample_t *sample, bool *tap_confirmed);

/* Playback requires a confirmed hardware tap and an above-threshold sample
   outside refractory. NO_TAP and BELOW_THRESHOLD do not populate the event.
   Distinct taps do not require an intervening below-threshold sample. */
impact_candidate_result_t impact_process_sample(const accel_sample_t *sample, bool tap_confirmed,
    bt_audio_play_result_t (*start_audio)(void), impact_event_t *event);

void impact_init(const impact_config_t *config);
bool impact_sample_exceeds_threshold(const accel_sample_t *sample);
impact_candidate_result_t impact_classify_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
impact_candidate_result_t impact_classify_threshold_crossing(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
bool impact_claim_candidate_for_audio(impact_candidate_result_t result, int64_t timestamp_us, uint32_t audio_duration_us, int64_t *audio_busy_until_us);
bool impact_validate_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
void impact_mark_accepted(int64_t timestamp_us);
