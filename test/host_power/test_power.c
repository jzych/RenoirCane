#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
#include "../../components/status_led/status_led.c"
#include "../../components/power/power.c"

static jmp_buf finish;
static void (*task_fn)(void *);
static TickType_t now, armed_at, pressed_at, amber_at, led_off_at;
static int pin, notification_count, scenario;
static bool led_held, rtc_pull, rtc_pd_disabled, rtc_kept_on, ext0_enabled, sensor_off, radio_stopped;
static unsigned levels[40], shift_count;
static uint8_t shifted[16];

BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                      UBaseType_t priority, TaskHandle_t *out) {
    (void)name; (void)stack; (void)arg; (void)priority;
    task_fn = fn; *out = (void *)1; return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { (void)task; longjmp(finish, 3); }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken) { (void)task; (void)woken; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) {
    (void)clear;
    if (ticks == 0) return 0;
    if (scenario == 3 || notification_count++) longjmp(finish, 2); /* no press / rejected bounce */
    pin = 0; pressed_at = now; return 1;
}
void vTaskDelay(TickType_t ticks) {
    now += ticks;
    if (scenario == 1 && now >= pressed_at + 50) pin = 1; /* short noise */
    if (scenario == 2 && !armed_at && now >= 150) pin = 1; /* held wake */
    if (armed_at && now >= pressed_at + 650) pin = 1; /* release first press */
}
esp_err_t gpio_config(const gpio_config_t *cfg) {
    if (cfg->pin_bit_mask == (1ULL << 33)) {
        assert(cfg->mode == GPIO_MODE_INPUT && cfg->pull_up_en == GPIO_PULLUP_ENABLE);
        assert(cfg->pull_down_en == GPIO_PULLDOWN_DISABLE && cfg->intr_type == GPIO_INTR_DISABLE);
    }
    return ESP_OK;
}
esp_err_t gpio_install_isr_service(int flags) { assert(flags == ESP_INTR_FLAG_IRAM); return ESP_ERR_INVALID_STATE; }
esp_err_t gpio_isr_handler_add(gpio_num_t p, void (*fn)(void *), void *a) { (void)fn; (void)a; assert(p == 33); return ESP_OK; }
esp_err_t gpio_isr_handler_remove(gpio_num_t p) { (void)p; return ESP_OK; }
esp_err_t gpio_intr_disable(gpio_num_t p) { assert(p == 33); return ESP_OK; }
esp_err_t gpio_intr_enable(gpio_num_t p) { assert(p == 33 && pin == 1); armed_at = now; return ESP_OK; }
esp_err_t gpio_set_intr_type(gpio_num_t p, int t) { assert(p == 33 && t == GPIO_INTR_NEGEDGE); return ESP_OK; }
int gpio_get_level(gpio_num_t p) { assert(p == 33); return pin; }
esp_err_t gpio_set_level(gpio_num_t p, uint32_t value) {
    levels[p] = value;
    if (p == 13 && value == 0 && power_is_entering_standby()) {
        assert(now >= pressed_at + 50);
        amber_at = now;
    }
    if (p == 12 && value == 1) {
        unsigned n = shift_count++ % 128;
        shifted[n / 8] = (uint8_t)((shifted[n / 8] << 1) | levels[2]);
    }
    if (p == 13 && value == 1) led_off_at = now;
    return ESP_OK;
}
esp_err_t rtc_gpio_hold_dis(gpio_num_t p) { assert(p == 13); led_held = false; return ESP_OK; }
esp_err_t rtc_gpio_hold_en(gpio_num_t p) { assert(p == 13 && levels[13] == 1); led_held = true; return ESP_OK; }
esp_err_t rtc_gpio_deinit(gpio_num_t p) { assert(p == 33); return ESP_OK; }
esp_err_t rtc_gpio_init(gpio_num_t p) { assert(p == 33); return ESP_OK; }
esp_err_t rtc_gpio_set_direction(gpio_num_t p, int mode) { assert(p == 33 && mode == RTC_GPIO_MODE_INPUT_ONLY); return ESP_OK; }
esp_err_t rtc_gpio_pullup_en(gpio_num_t p) { assert(p == 33); rtc_pull = true; return ESP_OK; }
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t p) { assert(p == 33); rtc_pd_disabled = true; return ESP_OK; }
esp_err_t esp_sleep_pd_config(int domain, int option) {
    assert(domain == ESP_PD_DOMAIN_RTC_PERIPH && option == ESP_PD_OPTION_ON);
    rtc_kept_on = true; return ESP_OK;
}
esp_err_t esp_sleep_enable_ext0_wakeup(gpio_num_t p, int level) {
    assert(p == 33 && level == 0); ext0_enabled = true; return ESP_OK;
}
void esp_deep_sleep_start(void) {
    assert(pin == 1 && led_held && rtc_pull && rtc_pd_disabled && rtc_kept_on && ext0_enabled);
    assert(levels[13] == 1 && sensor_off && radio_stopped);
    assert(led_off_at >= amber_at + 500);
    longjmp(finish, 1);
}
bool bluetooth_audio_shutdown(uint32_t ms) {
    assert(ms == 10000 && power_is_entering_standby());
    radio_stopped = true;
    /* Late BT status must not override the standby LED. */
    status_led_set(STATUS_LED_READY);
    assert(levels[13] == 1);
    return false; /* Timeout must still permit standby. */
}
void accelerometer_prepare_for_sleep(void) { assert(radio_stopped); sensor_off = true; }

int main(void) {
    for (scenario = 0; scenario < 4; ++scenario) {
        s_power_task = NULL;
        s_entering_standby = false;
        now = armed_at = pressed_at = amber_at = led_off_at = notification_count = 0;
        pin = scenario == 2 ? 0 : 1;
        led_held = rtc_pull = rtc_pd_disabled = rtc_kept_on = ext0_enabled = sensor_off = radio_stopped = false;
        shift_count = 0; memset(shifted, 0, sizeof(shifted));
        status_led_init();
        status_led_set(STATUS_LED_WAKING);
        assert(shifted[5] == 16 && shifted[6] == 16 && shifted[7] == 0); /* cyan BGR */
        shift_count = 0; memset(shifted, 0, sizeof(shifted));
        power_init();
        assert(task_fn != NULL);
        int outcome = setjmp(finish);
        if (outcome == 0) task_fn(NULL);
        if (scenario == 1 || scenario == 3) {
            assert(outcome == 2 && !radio_stopped && !power_is_entering_standby());
        } else {
            assert(outcome == 1 && power_is_entering_standby());
            if (scenario == 2) assert(armed_at >= 200); /* stable release before rearming */
            assert(shifted[5] == 0 && shifted[6] == 8 && shifted[7] == 16); /* amber BGR */
        }
    }
    puts("PASS: debounce/no-button/held-wake, ordered shutdown, timeout fallback, RTC pulls, LED hold/priority");
    return 0;
}
