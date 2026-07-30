#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "accelerometer.h"
typedef struct { app_impact_axis_t axis; int16_t threshold_raw; uint32_t refractory_ms; } impact_config_t;
typedef struct { int64_t timestamp_us; accel_sample_t sample; uint32_t axis_abs_raw; } impact_event_t;
typedef enum {
    IMPACT_CANDIDATE_BELOW_THRESHOLD,
    IMPACT_CANDIDATE_REFRACTORY,
    IMPACT_CANDIDATE_PROCESSED,
} impact_candidate_result_t;

void impact_init(const impact_config_t *config);
impact_candidate_result_t impact_classify_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
bool impact_claim_candidate_for_audio(impact_candidate_result_t result, int64_t timestamp_us, uint32_t audio_duration_us, int64_t *audio_busy_until_us);
bool impact_validate_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
void impact_mark_accepted(int64_t timestamp_us);
