#include "audio.h"
#include "accelerometer.h"
#include "battery.h"
#include "esp_log.h"
#include "impact.h"
#include "status_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CANE";
static TaskHandle_t s_impact_task;

static void IRAM_ATTR impact_irq_notify(void) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_impact_task, &higher_priority_task_woken);
    if (higher_priority_task_woken) portYIELD_FROM_ISR();
}

static void impact_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        accelerometer_clear_interrupt();
        accel_sample_t sample;
        impact_event_t event;
        if (accelerometer_read_sample(&sample) && impact_validate_candidate(&sample, sample.timestamp_us, &event)) {
            impact_mark_accepted(event.timestamp_us);
            ESP_LOGI("IMPACT", "accepted t_us=%lld axis_raw=%lu x=%d y=%d z=%d", event.timestamp_us, (unsigned long)event.axis_abs_raw, event.sample.x, event.sample.y, event.sample.z);
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

void app_main(void) {
    impact_config_t config = {
        .axis = APP_IMPACT_ACTIVE_AXIS,
        .threshold_raw = APP_IMPACT_THRESHOLD_RAW,
        .refractory_ms = APP_IMPACT_REFRACTORY_MS,
    };
    status_led_init();
    status_led_set(STATUS_LED_BOOT);
    battery_init();
    audio_init();
    impact_init(&config);
    xTaskCreate(impact_task, "impact", 3072, NULL, 6, &s_impact_task);
    if (!accelerometer_init(impact_irq_notify)) {
        status_led_set(STATUS_LED_ERROR);
        ESP_LOGE(TAG, "accelerometer initialization failed");
    } else {
        xTaskCreate(accel_diagnostic_task, "accel_diag", 3072, NULL, 5, NULL);
    }
    ESP_LOGI(TAG, "impact audio ECU booted; active_axis=%d threshold=%d battery=%.2fV", config.axis, config.threshold_raw, battery_get_voltage());
}
