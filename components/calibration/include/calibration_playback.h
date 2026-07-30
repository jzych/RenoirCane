#pragma once

#include <stdbool.h>

#include "bluetooth_audio.h"
#include "calibration_protocol.h"

typedef struct {
    calibration_impact_outcome_t outcome;
    bool audio_busy_skipped;
} calibration_playback_outcome_t;

calibration_playback_outcome_t calibration_playback_map_result(bt_audio_play_result_t result);
