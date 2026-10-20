#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
#include "esp_sleep.h"
#include "../../components/status_led/status_led.c"
#include "../../components/power/power.c"

static jmp_buf finish;
static void (*task_fn)(void *);
static TickType_t now, amber_at, led_off_at;
static int pin, scenario;
static bool led_held, rtc_pull, rtc_pd_disabled, rtc_kept_on, ext0_enabled, sensor_off, radio_stopped;
static unsigned levels[40], shift_count;
static uint8_t shifted[16];

bool audio_is_active(void) { return false; }

BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                      UBaseType_t priority, TaskHandle_t *out) {
    (void)name; (void)stack; (void)arg; (void)priority;
    task_fn = fn; *out = (void *)1; return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { (void)task; longjmp(finish, 3); }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken) { (void)task; (void)woken; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) {
    (void)clear; (void)ticks;
    longjmp(finish, 2); /* No switch-edge notification is delivered in this test. */
}
void vTaskDelay(TickType_t ticks) {
    now += ticks;
    if (scenario == 0 && now >= 200) longjmp(finish, 2); /* ON held low stays awake. */
    if (scenario == 2 && now >= 100) pin = 1; /* ON -> OFF */
    if (scenario == 3) { /* Brief OFF glitch, then ON remains held. */
        pin = now >= 20 && now < 40 ? 1 : 0;
        if (now >= 200) longjmp(finish, 2);
    }
    if (scenario == 4 && radio_stopped) pin = 0; /* ON again during shutdown. */
}
int64_t esp_timer_get_time(void) { return (int64_t)now * 1000; }
esp_err_t gpio_config(const gpio_config_t *cfg) {
    if (cfg->pin_bit_mask == (1ULL << 33)) {
        assert(cfg->mode == GPIO_MODE_INPUT && cfg->pull_up_en == GPIO_PULLUP_ENABLE);
        assert(cfg->pull_down_en == GPIO_PULLDOWN_DISABLE && cfg->intr_type == GPIO_INTR_DISABLE);
    }
    return ESP_OK;
}
esp_err_t gpio_install_isr_service(int f) { (void)f; return ESP_ERR_INVALID_STATE; }
esp_err_t gpio_isr_handler_add(gpio_num_t p, void (*fn)(void *), void *a) { (void)p; (void)fn; (void)a; return ESP_OK; }
esp_err_t gpio_isr_handler_remove(gpio_num_t p) { (void)p; return ESP_OK; }
esp_err_t gpio_intr_disable(gpio_num_t p) { (void)p; return ESP_OK; }
esp_err_t gpio_intr_enable(gpio_num_t p) { (void)p; return ESP_OK; }
esp_err_t gpio_set_intr_type(gpio_num_t p, int t) { (void)p; (void)t; return ESP_OK; }
int gpio_get_level(gpio_num_t p) { assert(p == 33); return pin; }
esp_err_t gpio_set_level(gpio_num_t p, uint32_t value) {
    levels[p] = value;
    if (p == 13 && value == 0 && power_is_entering_standby()) {
        assert(now >= (scenario == 2 ? 150U : 50U));
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
esp_err_t rtc_gpio_set_direction(gpio_num_t p, int m) { assert(p == 33 && m == RTC_GPIO_MODE_INPUT_ONLY); return ESP_OK; }
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
    assert(led_held && rtc_pull && rtc_pd_disabled && rtc_kept_on && ext0_enabled);
    assert(levels[13] == 1 && sensor_off && radio_stopped);
    assert(led_off_at >= amber_at + 500);
    /* Stable OFF sleeps without another switch toggle or any release wait. */
    assert(pin == (scenario == 4 ? 0 : 1));
    longjmp(finish, 1);
}
bool bluetooth_audio_shutdown(uint32_t ms) {
    assert(ms == 10000 && power_is_entering_standby());
    radio_stopped = true;
    if (scenario == 4) pin = 0;
    status_led_set(STATUS_LED_READY);
    assert(levels[13] == 1);
    return false; /* Timeout still permits sleep (and EXT0 wake if already ON). */
}
void accelerometer_prepare_for_sleep(void) { assert(radio_stopped); sensor_off = true; }

int main(void) {
#if APP_FEATURE_SLEEP_SWITCH
    for (scenario = 0; scenario < 5; ++scenario) {
        s_power_task = NULL;
        s_entering_standby = false;
        now = amber_at = led_off_at = 0;
        pin = scenario == 1 || scenario == 4 ? 1 : 0;
        led_held = rtc_pull = rtc_pd_disabled = rtc_kept_on = ext0_enabled = sensor_off = radio_stopped = false;
        shift_count = 0; memset(shifted, 0, sizeof(shifted));
        status_led_init();
        status_led_set(STATUS_LED_WAKING);
        assert(shifted[5] == 16 && shifted[6] == 16 && shifted[7] == 0);
        shift_count = 0; memset(shifted, 0, sizeof(shifted));
        power_init();
        assert(task_fn != NULL);
        int outcome = setjmp(finish);
        if (outcome == 0) task_fn(NULL);
        if (scenario == 0 || scenario == 3) {
            assert(outcome == 2 && !radio_stopped && !power_is_entering_standby());
        } else {
            assert(outcome == 1 && power_is_entering_standby());
            assert(shifted[5] == 0 && shifted[6] == 8 && shifted[7] == 16);
        }
    }
    puts("PASS: held ON, OFF at boot/after ON without edges, OFF bounce, fast ON, RTC/LED/shutdown");
#else
    pin = 1;
    power_init();
    assert(task_fn == NULL && !power_is_entering_standby() && !radio_stopped);
    puts("PASS: disabled switch support leaves an unfitted GPIO33 awake");
#endif
    return 0;
}
