#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

typedef struct { int16_t x; int16_t y; int16_t z; int64_t timestamp_us; } accel_sample_t;
typedef struct { app_impact_axis_t axis; int16_t threshold_raw; uint32_t refractory_ms; } impact_config_t;
typedef struct { int64_t timestamp_us; accel_sample_t sample; uint32_t axis_abs_raw; } impact_event_t;

void impact_init(const impact_config_t *config);
bool impact_validate_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
void impact_mark_accepted(int64_t timestamp_us);
