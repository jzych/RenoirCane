#include "audio.h"
#include "accelerometer.h"
#include "battery.h"
#include "bluetooth_audio.h"
#include "debug_console.h"
#include "esp_log.h"
#include "impact.h"
#include "status_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "calibration_mode.h"

#ifndef CALIBRATION_MODE
#define CALIBRATION_MODE 0
#endif

#if !CALIBRATION_MODE
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
        bool tap_candidate = accelerometer_read_impact_status();
        accelerometer_clear_interrupt();
        accel_sample_t sample;
        impact_event_t event;
        if (tap_candidate && accelerometer_read_sample(&sample) && impact_validate_candidate(&sample, sample.timestamp_us, &event)) {
            impact_mark_accepted(event.timestamp_us);
            bt_audio_play_result_t playback_result = bluetooth_audio_start_impact();
            ESP_LOGI("IMPACT", "accepted t_us=%lld axis_raw=%lu x=%d y=%d z=%d playback=%d", event.timestamp_us, (unsigned long)event.axis_abs_raw, event.sample.x, event.sample.y, event.sample.z, playback_result);
        }
    }
}

static void accel_diagnostic_task(void *arg) {
    (void)arg;
    for (;;) {
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
    status_led_set(STATUS_LED_BOOT);
    battery_init();
    audio_init();
    if (bluetooth_audio_init(bluetooth_state_changed)) {
        bluetooth_audio_start();
    } else {
        status_led_set(STATUS_LED_ERROR);
    }
    impact_init(&config);
    xTaskCreate(impact_task, "impact", 3072, NULL, 6, &s_impact_task);
    if (!accelerometer_init(impact_irq_notify)) {
        status_led_set(STATUS_LED_ERROR);
        ESP_LOGE(TAG, "accelerometer initialization failed");
    } else {
        xTaskCreate(accel_diagnostic_task, "accel_diag", 3072, NULL, 5, NULL);
    }
    debug_console_init();
    ESP_LOGI(TAG, "impact audio ECU booted; active_axis=%d threshold=%d battery=%.2fV", config.axis, config.threshold_raw, battery_get_voltage());
#endif
}
#endif
