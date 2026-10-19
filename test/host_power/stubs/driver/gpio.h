#pragma once
#include "sdk.h"

/* Minimal GPIO names needed to compile app_config on the host. */
typedef int gpio_num_t;
#define GPIO_NUM_33 33
#define GPIO_NUM_2 2
#define GPIO_NUM_4 4
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_14 14
#define GPIO_NUM_21 21
#define GPIO_NUM_22 22
#define GPIO_NUM_34 34
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_POSEDGE 1
#define GPIO_INTR_NEGEDGE 2
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_install_isr_service(int);
esp_err_t gpio_isr_handler_add(gpio_num_t, void (*)(void *), void *);
esp_err_t gpio_isr_handler_remove(gpio_num_t);
esp_err_t gpio_intr_disable(gpio_num_t);
esp_err_t gpio_intr_enable(gpio_num_t);
esp_err_t gpio_set_intr_type(gpio_num_t, int);
int gpio_get_level(gpio_num_t);
esp_err_t gpio_set_level(gpio_num_t, uint32_t);
