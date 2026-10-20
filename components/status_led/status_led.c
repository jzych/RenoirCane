#include "status_led.h"

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "audio.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#define PLAYBACK_POLL_MS 10U

static portMUX_TYPE s_led_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_standby;
static status_led_state_t s_state = STATUS_LED_OFF;
static TaskHandle_t s_playback_task;
static bool s_monitor_failed;

static const uint8_t colors[][3] = {
    [STATUS_LED_OFF] = {0, 0, 0},
    [STATUS_LED_BOOT] = {8, 8, 8},
    [STATUS_LED_BT_CONNECTING] = {0, 0, 16},
    [STATUS_LED_READY] = {0, 16, 0},
    [STATUS_LED_PLAYING] = {0, 12, 12},
    [STATUS_LED_ERROR] = {16, 0, 0},
    [STATUS_LED_ENTERING_STANDBY] = {16, 8, 0},
    [STATUS_LED_WAKING] = {0, 16, 16},
};

static void write_byte(uint8_t value) {
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(APP_LED_DATA_GPIO, (value >> bit) & 1);
        gpio_set_level(APP_LED_CLOCK_GPIO, 1);
        gpio_set_level(APP_LED_CLOCK_GPIO, 0);
    }
}

static void write_rgb(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 4; ++i) write_byte(0);
    write_byte(0xE2);
    write_byte(b);
    write_byte(g);
    write_byte(r);
    for (int i = 0; i < 4; ++i) write_byte(0xFF);
}

/* Caller holds s_led_lock for both state changes and the complete APA102 frame. */
static void set_locked(status_led_state_t state) {
    /* Late Bluetooth status cannot replace amber or power the LED back on. */
    if (s_standby && state != STATUS_LED_OFF && state != STATUS_LED_ENTERING_STANDBY) return;
    if (s_monitor_failed && state != STATUS_LED_OFF &&
        state != STATUS_LED_ENTERING_STANDBY && state != STATUS_LED_WAKING) {
        state = STATUS_LED_ERROR;
    }
    if (state == STATUS_LED_ENTERING_STANDBY) s_standby = true;
    s_state = state;
    if (state == STATUS_LED_OFF) {
        gpio_set_level(APP_LED_POWER_GPIO, 1);
    } else {
        gpio_set_level(APP_LED_POWER_GPIO, 0);
        write_rgb(colors[state][0], colors[state][1], colors[state][2]);
    }
}

static void playback_task(void *arg) {
    (void)arg;
    for (;;) {
        /* Release the audio lock before taking the LED lock. Recheck the LED
           state under its lock so a disconnect/error/standby always wins. */
        bool active = audio_is_active();
        portENTER_CRITICAL(&s_led_lock);
        if (!s_standby && (s_state == STATUS_LED_READY || s_state == STATUS_LED_PLAYING)) {
            status_led_state_t state = active ? STATUS_LED_PLAYING : STATUS_LED_READY;
            if (state != s_state) set_locked(state);
        }
        portEXIT_CRITICAL(&s_led_lock);
        vTaskDelay(pdMS_TO_TICKS(PLAYBACK_POLL_MS));
    }
}

void status_led_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << APP_LED_DATA_GPIO) | (1ULL << APP_LED_CLOCK_GPIO) | (1ULL << APP_LED_POWER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    /* Release the LED-off latch retained across deep sleep. */
    rtc_gpio_hold_dis(APP_LED_POWER_GPIO);
    gpio_config(&cfg);
    gpio_set_level(APP_LED_POWER_GPIO, 1);
    s_standby = false;
    s_state = STATUS_LED_OFF;
    if (s_playback_task == NULL &&
        xTaskCreate(playback_task, "led_playback", 2048U, NULL, 3U, &s_playback_task) != pdPASS) {
        s_monitor_failed = true;
        status_led_set(STATUS_LED_ERROR);
    }
}

void status_led_power_off(void) {
    portENTER_CRITICAL(&s_led_lock);
    s_state = STATUS_LED_OFF;
    gpio_set_level(APP_LED_POWER_GPIO, 1);
    gpio_set_level(APP_LED_DATA_GPIO, 0);
    gpio_set_level(APP_LED_CLOCK_GPIO, 0);
    portEXIT_CRITICAL(&s_led_lock);
}

void status_led_set(status_led_state_t state) {
    if ((unsigned)state >= sizeof(colors) / sizeof(colors[0])) return;
    portENTER_CRITICAL(&s_led_lock);
    set_locked(state);
    portEXIT_CRITICAL(&s_led_lock);
}

void status_led_on_bluetooth_state(bt_audio_state_t state) {
    switch (state) {
        case BT_AUDIO_DISABLED:
        case BT_AUDIO_CONNECTING:
            status_led_set(STATUS_LED_BT_CONNECTING);
            break;
        case BT_AUDIO_CONNECTED:
        case BT_AUDIO_STREAMING:
            status_led_set(STATUS_LED_READY);
            break;
        case BT_AUDIO_ERROR:
            status_led_set(STATUS_LED_ERROR);
            break;
    }
}
