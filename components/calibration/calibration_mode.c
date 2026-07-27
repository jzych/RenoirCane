#include "calibration_mode.h"

#include <stddef.h>

#include "accelerometer.h"
#include "esp_log.h"
#include "status_led.h"

static const char *TAG = "CAL_MODE";

void calibration_mode_start(void) {
    status_led_init();
    status_led_set(STATUS_LED_BOOT);
    if (!accelerometer_init(NULL) || !accelerometer_set_data_ready_callback(NULL)) {
        status_led_set(STATUS_LED_ERROR);
        ESP_LOGE(TAG, "calibration accelerometer initialization failed");
        return;
    }
    ESP_LOGI(TAG, "calibration sensor mode initialized");
}
