#include "power.h"

#include "accelerometer.h"
#include "app_config.h"
#include "bluetooth_audio.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_button.h"
#include "status_led.h"

#define RELEASE_POLL_MS 10U

static const char *TAG = "POWER";
static TaskHandle_t s_power_task;
static portMUX_TYPE s_power_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_entering_standby;

bool power_is_entering_standby(void) {
    portENTER_CRITICAL(&s_power_lock);
    bool entering = s_entering_standby;
    portEXIT_CRITICAL(&s_power_lock);
    return entering;
}

static void IRAM_ATTR button_isr(void *arg) {
    (void)arg;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_power_task, &woken);
    if (woken) portYIELD_FROM_ISR();
}

static void wait_for_release(void) {
    /* Also rejects bounce from the press that woke the chip. */
    for (;;) {
        while (gpio_get_level(APP_SLEEP_BUTTON_GPIO) == 0) {
            vTaskDelay(pdMS_TO_TICKS(RELEASE_POLL_MS));
        }
        vTaskDelay(pdMS_TO_TICKS(APP_SLEEP_BUTTON_DEBOUNCE_MS));
        if (gpio_get_level(APP_SLEEP_BUTTON_GPIO) != 0) return;
    }
}

static bool prepare_wakeup(void) {
    /* EXT0 needs RTC peripherals powered for its internal pull-up to persist. */
    return esp_sleep_enable_ext0_wakeup(APP_SLEEP_BUTTON_GPIO, 0) == ESP_OK &&
           esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON) == ESP_OK &&
           rtc_gpio_pullup_en(APP_SLEEP_BUTTON_GPIO) == ESP_OK &&
           rtc_gpio_pulldown_dis(APP_SLEEP_BUTTON_GPIO) == ESP_OK;
}

static void power_task(void *arg) {
    (void)arg;
    power_button_t button;
    power_button_init(&button);
    wait_for_release();
    if (gpio_set_intr_type(APP_SLEEP_BUTTON_GPIO, GPIO_INTR_NEGEDGE) != ESP_OK ||
        gpio_intr_enable(APP_SLEEP_BUTTON_GPIO) != ESP_OK) {
        ESP_LOGE(TAG, "standby button interrupt enable failed");
        status_led_set(STATUS_LED_ERROR);
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        power_button_note_falling_edge(&button);
        vTaskDelay(pdMS_TO_TICKS(APP_SLEEP_BUTTON_DEBOUNCE_MS));
        if (!power_button_confirm_press(&button, gpio_get_level(APP_SLEEP_BUTTON_GPIO) == 0)) continue;

        /* Verify the wake path before stopping normal services. */
        if (!prepare_wakeup()) {
            ESP_LOGE(TAG, "standby wake-source configuration failed");
            status_led_set(STATUS_LED_ERROR);
            continue;
        }
        (void)gpio_intr_disable(APP_SLEEP_BUTTON_GPIO);
        portENTER_CRITICAL(&s_power_lock);
        s_entering_standby = true;
        portEXIT_CRITICAL(&s_power_lock);
        ESP_LOGI(TAG, "entering standby");
        status_led_set(STATUS_LED_ENTERING_STANDBY);
        vTaskDelay(pdMS_TO_TICKS(APP_SLEEP_ENTER_LED_MS));
        status_led_power_off();

        (void)bluetooth_audio_shutdown(APP_SLEEP_BT_SHUTDOWN_TIMEOUT_MS);
        accelerometer_prepare_for_sleep();
        wait_for_release();

        /* EXT0 selects the RTC mux. Set it explicitly before sleeping. */
        if (rtc_gpio_init(APP_SLEEP_BUTTON_GPIO) != ESP_OK ||
            rtc_gpio_set_direction(APP_SLEEP_BUTTON_GPIO, RTC_GPIO_MODE_INPUT_ONLY) != ESP_OK ||
            rtc_gpio_pullup_en(APP_SLEEP_BUTTON_GPIO) != ESP_OK ||
            rtc_gpio_pulldown_dis(APP_SLEEP_BUTTON_GPIO) != ESP_OK ||
            rtc_gpio_hold_en(APP_LED_POWER_GPIO) != ESP_OK) {
            ESP_LOGE(TAG, "standby RTC pin setup failed; reset required");
            vTaskDelete(NULL);
            return;
        }
        ESP_LOGI(TAG, "deep sleep; wake on GPIO%d low", APP_SLEEP_BUTTON_GPIO);
        esp_deep_sleep_start();
    }
}

void power_init(void) {
    if (s_power_task != NULL) return;
    /* EXT0 wake leaves the pad in RTC mode. Restore digital GPIO. */
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << APP_SLEEP_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (rtc_gpio_deinit(APP_SLEEP_BUTTON_GPIO) != ESP_OK || gpio_config(&config) != ESP_OK) {
        ESP_LOGE(TAG, "standby button configuration failed");
        status_led_set(STATUS_LED_ERROR);
        return;
    }
    esp_err_t error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if ((error != ESP_OK && error != ESP_ERR_INVALID_STATE) ||
        gpio_isr_handler_add(APP_SLEEP_BUTTON_GPIO, button_isr, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "standby button ISR setup failed");
        status_led_set(STATUS_LED_ERROR);
        return;
    }
    if (xTaskCreate(power_task, "power", 3072U, NULL, 5U, &s_power_task) != pdPASS) {
        (void)gpio_isr_handler_remove(APP_SLEEP_BUTTON_GPIO);
        s_power_task = NULL;
        ESP_LOGE(TAG, "standby task allocation failed");
        status_led_set(STATUS_LED_ERROR);
    }
}
