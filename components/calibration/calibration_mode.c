#include "calibration_mode.h"

#include <stddef.h>

#include "accelerometer.h"
#include "audio.h"
#include "app_config.h"
#include "bluetooth_audio.h"
#include "calibration_log.h"
#include "calibration_stream.h"
#include "esp_log.h"
#include "impact.h"
#include "status_led.h"

static const char *TAG = "CAL_MODE";

void calibration_mode_start(void) {
    status_led_init();
    status_led_set(STATUS_LED_BOOT);
    if (!calibration_log_init()) {
        status_led_set(STATUS_LED_ERROR);
        return;
    }
    audio_init();
    impact_config_t impact_config = {
        .axis = APP_IMPACT_ACTIVE_AXIS,
        .threshold_raw = APP_IMPACT_THRESHOLD_RAW,
        .refractory_ms = APP_IMPACT_REFRACTORY_MS,
    };
    impact_init(&impact_config);
    if (!calibration_stream_start()) {
        status_led_set(STATUS_LED_ERROR);
        return;
    }
    if (bluetooth_audio_init(status_led_on_bluetooth_state)) {
        bluetooth_audio_start();
    } else {
        status_led_set(STATUS_LED_ERROR);
    }
    if (!accelerometer_init(calibration_stream_on_impact_from_isr) || !accelerometer_set_data_ready_callback(calibration_stream_on_data_ready_from_isr)) {
        status_led_set(STATUS_LED_ERROR);
        ESP_LOGE(TAG, "calibration accelerometer initialization failed");
        return;
    }
    accel_sample_t initial_sample;
    (void)accelerometer_read_sample(&initial_sample);
}
