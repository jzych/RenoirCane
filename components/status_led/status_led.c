#include "status_led.h"
#include <stdint.h>
#include "app_config.h"
#include "driver/gpio.h"

static void write_byte(uint8_t value) { for (int bit = 7; bit >= 0; --bit) { gpio_set_level(APP_LED_DATA_GPIO, (value >> bit) & 1); gpio_set_level(APP_LED_CLOCK_GPIO, 1); gpio_set_level(APP_LED_CLOCK_GPIO, 0); } }
static void write_rgb(uint8_t r, uint8_t g, uint8_t b) { for (int i = 0; i < 4; ++i) write_byte(0); write_byte(0xE2); write_byte(b); write_byte(g); write_byte(r); for (int i = 0; i < 4; ++i) write_byte(0xFF); }
void status_led_init(void) { gpio_config_t cfg = {.pin_bit_mask = (1ULL << APP_LED_DATA_GPIO) | (1ULL << APP_LED_CLOCK_GPIO) | (1ULL << APP_LED_POWER_GPIO), .mode = GPIO_MODE_OUTPUT, .pull_down_en = GPIO_PULLDOWN_DISABLE, .pull_up_en = GPIO_PULLUP_DISABLE, .intr_type = GPIO_INTR_DISABLE}; gpio_config(&cfg); gpio_set_level(APP_LED_POWER_GPIO, 1); }
void status_led_power_off(void) { gpio_set_level(APP_LED_POWER_GPIO, 1); }
void status_led_set(status_led_state_t state) { static const uint8_t colors[][3] = {{0,0,0},{8,8,8},{0,0,16},{0,16,0},{0,12,12},{16,0,0}}; if (state == STATUS_LED_OFF) { status_led_power_off(); return; } gpio_set_level(APP_LED_POWER_GPIO, 0); write_rgb(colors[state][0], colors[state][1], colors[state][2]); }
