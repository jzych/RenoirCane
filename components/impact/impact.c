#include "impact.h"

#include <limits.h>

static impact_config_t s_config;
static int64_t s_last_accepted_us = INT64_MIN;

static int16_t selected_axis(const accel_sample_t *sample) {
    switch (s_config.axis) {
        case APP_IMPACT_AXIS_X: return sample->x;
        case APP_IMPACT_AXIS_Y: return sample->y;
        case APP_IMPACT_AXIS_Z: return sample->z;
        default: return 0;
    }
}

void impact_init(const impact_config_t *config) { s_config = *config; s_last_accepted_us = INT64_MIN; }

impact_candidate_result_t impact_classify_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event) {
    int32_t raw = selected_axis(sample);
    uint32_t magnitude = (uint32_t)(raw < 0 ? -raw : raw);
    if (magnitude < (uint32_t)s_config.threshold_raw) return IMPACT_CANDIDATE_BELOW_THRESHOLD;
    event->timestamp_us = now_us;
    event->sample = *sample;
    event->axis_abs_raw = magnitude;
    if (now_us - s_last_accepted_us < (int64_t)s_config.refractory_ms * 1000) return IMPACT_CANDIDATE_REFRACTORY;
    return IMPACT_CANDIDATE_PROCESSED;
}

bool impact_claim_candidate_for_audio(impact_candidate_result_t result, int64_t timestamp_us, uint32_t audio_duration_us, int64_t *audio_busy_until_us) {
    if (result != IMPACT_CANDIDATE_PROCESSED || timestamp_us < *audio_busy_until_us) return false;
    *audio_busy_until_us = timestamp_us + (int64_t)audio_duration_us;
    return true;
}

bool impact_validate_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event) {
    return impact_classify_candidate(sample, now_us, event) == IMPACT_CANDIDATE_PROCESSED;
}

void impact_mark_accepted(int64_t timestamp_us) { s_last_accepted_us = timestamp_us; }
