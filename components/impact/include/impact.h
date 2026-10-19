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
} impact_candidate_result_t;

/* Called once per data-ready sample by both firmware modes. Playback is tried
   only for a new crossing outside refractory. BELOW_THRESHOLD also means an
   already-consumed crossing; the event is valid for every other result. */
impact_candidate_result_t impact_process_sample(const accel_sample_t *sample,
    bt_audio_play_result_t (*start_audio)(void), impact_event_t *event);

void impact_init(const impact_config_t *config);
bool impact_sample_exceeds_threshold(const accel_sample_t *sample);
impact_candidate_result_t impact_classify_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
impact_candidate_result_t impact_classify_threshold_crossing(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
bool impact_claim_candidate_for_audio(impact_candidate_result_t result, int64_t timestamp_us, uint32_t audio_duration_us, int64_t *audio_busy_until_us);
bool impact_validate_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
void impact_mark_accepted(int64_t timestamp_us);
