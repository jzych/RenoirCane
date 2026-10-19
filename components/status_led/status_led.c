#include "status_led.h"

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_led_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_standby;

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
}

void status_led_power_off(void) {
    portENTER_CRITICAL(&s_led_lock);
    gpio_set_level(APP_LED_POWER_GPIO, 1);
    gpio_set_level(APP_LED_DATA_GPIO, 0);
    gpio_set_level(APP_LED_CLOCK_GPIO, 0);
    portEXIT_CRITICAL(&s_led_lock);
}

void status_led_set(status_led_state_t state) {
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
    if ((unsigned)state >= sizeof(colors) / sizeof(colors[0])) return;
    portENTER_CRITICAL(&s_led_lock);
    /* Late Bluetooth status cannot replace amber or power the LED back on. */
    if (!s_standby || state == STATUS_LED_OFF || state == STATUS_LED_ENTERING_STANDBY) {
        if (state == STATUS_LED_ENTERING_STANDBY) s_standby = true;
        if (state == STATUS_LED_OFF) {
            gpio_set_level(APP_LED_POWER_GPIO, 1);
        } else {
            gpio_set_level(APP_LED_POWER_GPIO, 0);
            write_rgb(colors[state][0], colors[state][1], colors[state][2]);
        }
    }
    portEXIT_CRITICAL(&s_led_lock);
}
