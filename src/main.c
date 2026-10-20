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
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (power_is_entering_standby()) continue;
        accel_sample_t sample;
        impact_event_t event;
        bool tap_confirmed;
        if (!impact_read_sample(true, &sample, &tap_confirmed)) continue;
        impact_candidate_result_t result = impact_process_sample(&sample, tap_confirmed, bluetooth_audio_start_impact, &event);
        if (result == IMPACT_CANDIDATE_PROCESSED) {
            ESP_LOGI("IMPACT", "accepted t_us=%lld axis_raw=%lu x=%d y=%d z=%d", event.timestamp_us, (unsigned long)event.axis_abs_raw, event.sample.x, event.sample.y, event.sample.z);
        }
    }
}

/* Keep diagnostics on their original lower-priority task, separate from the
   tap-notification loop. An in-flight I2C transaction can still hold the bus. */
static void accel_diagnostic_task(void *arg) {
    (void)arg;
    for (;;) {
        if (power_is_entering_standby()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        accel_sample_t sample;
        if (accelerometer_read_sample(&sample)) {
            ESP_LOGI("ACCEL", "x=%d y=%d z=%d", sample.x, sample.y, sample.z);
        } else {
            ESP_LOGW("ACCEL", "sample read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
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
    } else {
        xTaskCreate(accel_diagnostic_task, "accel_diag", 3072, NULL, 5, NULL);
    }
    debug_console_init();
    power_init();
    ESP_LOGI(TAG, "impact audio ECU booted; active_axis=%d threshold=%d battery=%.2fV", config.axis, config.threshold_raw, battery_get_voltage());
#endif
}
#endif
