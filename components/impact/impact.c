#include "impact.h"

static impact_config_t s_config;
static int64_t s_last_accepted_us;
static bool s_has_accepted_impact;
static bool s_selected_axis_above_threshold;

impact_candidate_result_t impact_process_sample(const accel_sample_t *sample,
    bt_audio_play_result_t (*start_audio)(void), impact_event_t *event) {
    impact_candidate_result_t result = impact_classify_threshold_crossing(sample, sample->timestamp_us, event);
    if (result != IMPACT_CANDIDATE_PROCESSED) return result;

    /* Preserve calibration's refractory policy: every eligible crossing starts
       refractory, including an attempt skipped by playback. */
    impact_mark_accepted(event->timestamp_us);
    switch (start_audio()) {
        case BT_AUDIO_PLAY_STARTED: return IMPACT_CANDIDATE_PROCESSED;
        case BT_AUDIO_PLAY_BUSY: return IMPACT_CANDIDATE_AUDIO_BUSY;
        default: return IMPACT_CANDIDATE_AUDIO_UNAVAILABLE;
    }
}

static int16_t selected_axis(const accel_sample_t *sample) {
    switch (s_config.axis) {
        case APP_IMPACT_AXIS_X: return sample->x;
        case APP_IMPACT_AXIS_Y: return sample->y;
        case APP_IMPACT_AXIS_Z: return sample->z;
        default: return 0;
    }
}

static uint32_t selected_axis_magnitude(const accel_sample_t *sample) {
    int32_t raw = selected_axis(sample);
    return (uint32_t)(raw < 0 ? -raw : raw);
}

void impact_init(const impact_config_t *config) {
    s_config = *config;
    s_has_accepted_impact = false;
    s_selected_axis_above_threshold = false;
}

bool impact_sample_exceeds_threshold(const accel_sample_t *sample) {
    return selected_axis_magnitude(sample) >= (uint32_t)s_config.threshold_raw;
}

impact_candidate_result_t impact_classify_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event) {
    uint32_t magnitude = selected_axis_magnitude(sample);
    if (!impact_sample_exceeds_threshold(sample)) return IMPACT_CANDIDATE_BELOW_THRESHOLD;
    event->timestamp_us = now_us;
    event->sample = *sample;
    event->axis_abs_raw = magnitude;
    if (s_has_accepted_impact && now_us - s_last_accepted_us < (int64_t)s_config.refractory_ms * 1000) {
        return IMPACT_CANDIDATE_REFRACTORY;
    }
    return IMPACT_CANDIDATE_PROCESSED;
}

impact_candidate_result_t impact_classify_threshold_crossing(const accel_sample_t *sample, int64_t now_us, impact_event_t *event) {
    bool above_threshold = impact_sample_exceeds_threshold(sample);
    bool is_new_crossing = above_threshold && !s_selected_axis_above_threshold;
    s_selected_axis_above_threshold = above_threshold;
    if (!is_new_crossing) return IMPACT_CANDIDATE_BELOW_THRESHOLD;
    return impact_classify_candidate(sample, now_us, event);
}

bool impact_claim_candidate_for_audio(impact_candidate_result_t result, int64_t timestamp_us, uint32_t audio_duration_us, int64_t *audio_busy_until_us) {
    if (result != IMPACT_CANDIDATE_PROCESSED || timestamp_us < *audio_busy_until_us) return false;
    *audio_busy_until_us = timestamp_us + (int64_t)audio_duration_us;
    return true;
}

bool impact_validate_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event) {
    return impact_classify_candidate(sample, now_us, event) == IMPACT_CANDIDATE_PROCESSED;
}

void impact_mark_accepted(int64_t timestamp_us) {
    s_last_accepted_us = timestamp_us;
    s_has_accepted_impact = true;
}
