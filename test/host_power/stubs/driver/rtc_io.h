#pragma once
#include "driver/gpio.h"
#define RTC_GPIO_MODE_INPUT_ONLY 1
esp_err_t rtc_gpio_deinit(gpio_num_t);
esp_err_t rtc_gpio_init(gpio_num_t);
esp_err_t rtc_gpio_set_direction(gpio_num_t, int);
esp_err_t rtc_gpio_pullup_en(gpio_num_t);
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t);
esp_err_t rtc_gpio_hold_en(gpio_num_t);
esp_err_t rtc_gpio_hold_dis(gpio_num_t);
