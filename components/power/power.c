#include "power.h"

#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_power_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_entering_standby;

bool power_is_entering_standby(void) {
    portENTER_CRITICAL(&s_power_lock);
    bool entering = s_entering_standby;
    portEXIT_CRITICAL(&s_power_lock);
    return entering;
}

#if APP_FEATURE_SLEEP_SWITCH
#include "accelerometer.h"
#include "bluetooth_audio.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "power_button.h"
#include "status_led.h"

#define SWITCH_POLL_MS 10U

static const char *TAG = "POWER";
static TaskHandle_t s_power_task;

static bool prepare_wakeup(void) {
    /* The open/OFF switch needs RTC pull-up retention until ON pulls it low. */
    return esp_sleep_enable_ext0_wakeup(APP_SLEEP_BUTTON_GPIO, 0) == ESP_OK &&
           esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON) == ESP_OK &&
           rtc_gpio_pullup_en(APP_SLEEP_BUTTON_GPIO) == ESP_OK &&
           rtc_gpio_pulldown_dis(APP_SLEEP_BUTTON_GPIO) == ESP_OK;
}

static void power_task(void *arg) {
    (void)arg;
    power_switch_debounce_t state;
    power_switch_debounce_init(&state);
    for (;;) {
        /* Read the level continuously, including OFF at power-up. No edge or
           release is needed; a held ON switch must keep normal operation alive. */
        bool is_off = gpio_get_level(APP_SLEEP_BUTTON_GPIO) != 0;
        if (!power_switch_should_sleep(&state, is_off, esp_timer_get_time())) {
            vTaskDelay(pdMS_TO_TICKS(SWITCH_POLL_MS));
            continue;
        }
        if (!prepare_wakeup()) {
            ESP_LOGE(TAG, "standby wake-source configuration failed");
            status_led_set(STATUS_LED_ERROR);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        portENTER_CRITICAL(&s_power_lock);
        s_entering_standby = true;
        portEXIT_CRITICAL(&s_power_lock);
        ESP_LOGI(TAG, "entering standby; switch OFF");
        status_led_set(STATUS_LED_ENTERING_STANDBY);
        vTaskDelay(pdMS_TO_TICKS(APP_SLEEP_ENTER_LED_MS));
        status_led_power_off();

        (void)bluetooth_audio_shutdown(APP_SLEEP_BT_SHUTDOWN_TIMEOUT_MS);
        accelerometer_prepare_for_sleep();
        /* There is no release wait for a latching switch. If it has already
           returned ON during shutdown, EXT0 wakes immediately and stays awake. */
        if (rtc_gpio_init(APP_SLEEP_BUTTON_GPIO) != ESP_OK ||
            rtc_gpio_set_direction(APP_SLEEP_BUTTON_GPIO, RTC_GPIO_MODE_INPUT_ONLY) != ESP_OK ||
            rtc_gpio_pullup_en(APP_SLEEP_BUTTON_GPIO) != ESP_OK ||
            rtc_gpio_pulldown_dis(APP_SLEEP_BUTTON_GPIO) != ESP_OK ||
            rtc_gpio_hold_en(APP_LED_POWER_GPIO) != ESP_OK) {
            ESP_LOGE(TAG, "standby RTC pin setup failed; reset required");
            vTaskDelete(NULL);
            return;
        }
        ESP_LOGI(TAG, "deep sleep; wake on GPIO%d low (switch ON)", APP_SLEEP_BUTTON_GPIO);
        esp_deep_sleep_start();
    }
}

void power_init(void) {
    if (s_power_task != NULL) return;
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << APP_SLEEP_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    /* EXT0 leaves the pad in RTC mode. Restore digital level reads after wake. */
    if (rtc_gpio_deinit(APP_SLEEP_BUTTON_GPIO) != ESP_OK || gpio_config(&config) != ESP_OK) {
        ESP_LOGE(TAG, "standby switch configuration failed");
        status_led_set(STATUS_LED_ERROR);
        return;
    }
    if (xTaskCreate(power_task, "power", 3072U, NULL, 5U, &s_power_task) != pdPASS) {
        s_power_task = NULL;
        ESP_LOGE(TAG, "standby task allocation failed");
        status_led_set(STATUS_LED_ERROR);
    }
}
#else
void power_init(void) {
    /* Boards without a fitted switch remain awake and do not configure GPIO33. */
}
#endif
