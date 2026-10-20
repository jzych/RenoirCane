#include "impact.h"

bool impact_read_sample(bool tap_pending, accel_sample_t *sample, bool *tap_confirmed) {
    *tap_confirmed = false;
    if (tap_pending) {
        *tap_confirmed = accelerometer_read_impact_status();
        accelerometer_clear_interrupt();
    }
    return accelerometer_read_sample(sample);
}
