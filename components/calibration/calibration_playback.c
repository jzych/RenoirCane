#include "calibration_playback.h"

calibration_playback_outcome_t calibration_playback_map_result(bt_audio_play_result_t result) {
    if (result == BT_AUDIO_PLAY_STARTED) {
        return (calibration_playback_outcome_t){
            .outcome = CAL_IMPACT_PROCESSED,
            .audio_busy_skipped = false,
        };
    }
    return (calibration_playback_outcome_t){
        .outcome = CAL_IMPACT_SKIPPED,
        .audio_busy_skipped = result == BT_AUDIO_PLAY_BUSY,
    };
}
