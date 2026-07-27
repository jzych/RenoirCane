#include "battery.h"
#include "app_config.h"
#include "driver/adc.h"
#include "driver/gpio.h"

void battery_init(void) { adc1_config_width(ADC_WIDTH_BIT_12); adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11); gpio_set_direction(APP_BAT_CHARGE_GPIO, GPIO_MODE_INPUT); }
float battery_get_voltage(void) { uint32_t total = 0; for (int i = 0; i < 16; ++i) total += adc1_get_raw(ADC1_CHANNEL_7); return ((float)(total / 16) / 4095.0f * 3.3f) * 3.7625f; }
bool battery_is_charging(void) { return gpio_get_level(APP_BAT_CHARGE_GPIO) == 0; }
