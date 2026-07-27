#include "calibration_mode.h"

#include <stddef.h>

#include "accelerometer.h"
#include "calibration_stream.h"
#include "esp_log.h"
#include "status_led.h"

static const char *TAG = "CAL_MODE";

void calibration_mode_start(void) {
    status_led_init();
    status_led_set(STATUS_LED_BOOT);
    if (!accelerometer_init(NULL) || !calibration_stream_start() || !accelerometer_set_data_ready_callback(calibration_stream_on_data_ready_from_isr)) {
        status_led_set(STATUS_LED_ERROR);
        ESP_LOGE(TAG, "calibration accelerometer initialization failed");
        return;
    }
    accel_sample_t initial_sample;
    (void)accelerometer_read_sample(&initial_sample);
}
