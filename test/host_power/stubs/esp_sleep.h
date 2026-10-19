#pragma once
#include "driver/gpio.h"
#define ESP_SLEEP_WAKEUP_UNDEFINED 0
#define ESP_SLEEP_WAKEUP_EXT0 2
#define ESP_PD_DOMAIN_RTC_PERIPH 1
#define ESP_PD_OPTION_ON 1
esp_err_t esp_sleep_enable_ext0_wakeup(gpio_num_t, int);
esp_err_t esp_sleep_pd_config(int, int);
void esp_deep_sleep_start(void);
