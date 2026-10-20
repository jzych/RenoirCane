#include "audio.h"
#include "accelerometer.h"
#include "battery.h"
#include "bluetooth_audio.h"
#include "debug_console.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "impact.h"
#include "power.h"
#include "status_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "calibration_mode.h"

#ifndef CALIBRATION_MODE
#define CALIBRATION_MODE 0
#endif

#if !CALIBRATION_MODE && !defined(UNIT_TESTING)
static const char *TAG = "CANE";
static TaskHandle_t s_impact_task;

static void bluetooth_state_changed(bt_audio_state_t state) {
    switch (state) {
        case BT_AUDIO_CONNECTING:
            status_led_set(STATUS_LED_BT_CONNECTING);
            break;
        case BT_AUDIO_CONNECTED:
            status_led_set(STATUS_LED_READY);
            break;
        case BT_AUDIO_STREAMING:
            status_led_set(STATUS_LED_PLAYING);
            break;
        case BT_AUDIO_ERROR:
            status_led_set(STATUS_LED_ERROR);
            break;
        case BT_AUDIO_DISABLED:
            break;
    }
}

static void IRAM_ATTR impact_irq_notify(void) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_impact_task, &higher_priority_task_woken);
    if (higher_priority_task_woken) portYIELD_FROM_ISR();
}

static void impact_task(void *arg) {
    (void)arg;
    int64_t last_diagnostic_us = 0;
    for (;;) {
        uint32_t taps = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        if (power_is_entering_standby()) continue;
        accel_sample_t sample;
        impact_event_t event;
        bool tap_confirmed;
        if (!impact_read_sample(taps != 0U, &sample, &tap_confirmed)) continue;
        impact_candidate_result_t result = impact_process_sample(&sample, tap_confirmed, bluetooth_audio_start_impact, &event);
        if (result == IMPACT_CANDIDATE_PROCESSED) {
            ESP_LOGI("IMPACT", "accepted t_us=%lld axis_raw=%lu x=%d y=%d z=%d", event.timestamp_us, (unsigned long)event.axis_abs_raw, event.sample.x, event.sample.y, event.sample.z);
        }
        /* The same task owns tap reads and once-per-second diagnostics. */
        if (sample.timestamp_us - last_diagnostic_us >= 1000000) {
            ESP_LOGI("ACCEL", "x=%d y=%d z=%d", sample.x, sample.y, sample.z);
            last_diagnostic_us = sample.timestamp_us;
        }
    }
}
#endif

#ifndef UNIT_TESTING
void app_main(void) {
#if CALIBRATION_MODE
    calibration_mode_start();
#else
    impact_config_t config = {
        .axis = APP_IMPACT_ACTIVE_AXIS,
        .threshold_raw = APP_IMPACT_THRESHOLD_RAW,
        .refractory_ms = APP_IMPACT_REFRACTORY_MS,
    };
    status_led_init();
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        status_led_set(STATUS_LED_WAKING);
        vTaskDelay(pdMS_TO_TICKS(APP_SLEEP_WAKE_LED_MS));
    }
    status_led_set(STATUS_LED_BOOT);
    battery_init();
    audio_init();
    if (bluetooth_audio_init(bluetooth_state_changed)) {
        bluetooth_audio_start();
    } else {
        status_led_set(STATUS_LED_ERROR);
    }
    impact_init(&config);
    if (xTaskCreate(impact_task, "impact", 3072, NULL, 6, &s_impact_task) != pdPASS ||
        !accelerometer_init(impact_irq_notify)) {
        status_led_set(STATUS_LED_ERROR);
        ESP_LOGE(TAG, "accelerometer initialization failed");
    }
    debug_console_init();
    power_init();
    ESP_LOGI(TAG, "impact audio ECU booted; active_axis=%d threshold=%d battery=%.2fV", config.axis, config.threshold_raw, battery_get_voltage());
#endif
}
#endif
