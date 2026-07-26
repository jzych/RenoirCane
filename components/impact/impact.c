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

bool impact_validate_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event) {
    int32_t raw = selected_axis(sample);
    uint32_t magnitude = (uint32_t)(raw < 0 ? -raw : raw);
    if (magnitude < (uint32_t)s_config.threshold_raw ||
        now_us - s_last_accepted_us < (int64_t)s_config.refractory_ms * 1000) return false;
    event->timestamp_us = now_us;
    event->sample = *sample;
    event->axis_abs_raw = magnitude;
    return true;
}

void impact_mark_accepted(int64_t timestamp_us) { s_last_accepted_us = timestamp_us; }
